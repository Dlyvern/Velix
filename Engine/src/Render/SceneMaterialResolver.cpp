#include "Engine/Render/SceneMaterialResolver.hpp"

#include "Core/Logger.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Caches/Hash.hpp"
#include "Engine/Shaders/ShaderCompiler.hpp"

#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    std::size_t hashFloat(float value)
    {
        if (!std::isfinite(value))
            return 0u;

        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(float));
        return std::hash<uint32_t>()(bits);
    }

    bool fileExists(const std::filesystem::path &path)
    {
        std::error_code errorCode;
        return std::filesystem::exists(path, errorCode) && !errorCode;
    }
}

void SceneMaterialResolver::beginFrame(int maxNewMaterialLoads)
{
    m_newMaterialLoadsThisFrame = 0;
    m_maxNewMaterialLoadsPerFrame = std::max(maxNewMaterialLoads, 0);
}

Material::SharedPtr SceneMaterialResolver::resolveMaterialOverrideFromPath(const std::string &materialPath)
{
    if (materialPath.empty())
        return nullptr;

    std::string normalizedMaterialPath = materialPath;
    if (!looksLikeWindowsAbsolutePath(materialPath))
        normalizedMaterialPath = makeAbsoluteNormalized(std::filesystem::path(materialPath)).string();

    if (auto cachedMaterialIt = m_materialsByAssetPath.find(normalizedMaterialPath); cachedMaterialIt != m_materialsByAssetPath.end())
        return cachedMaterialIt->second;

    if (m_failedMaterialAssetPaths.find(normalizedMaterialPath) != m_failedMaterialAssetPaths.end())
        return nullptr;

    if (!consumeLoadBudget())
        return nullptr;

    auto materialAsset = AssetsLoader::loadMaterial(normalizedMaterialPath);
    if (!materialAsset.has_value())
    {
        const auto [_, inserted] = m_failedMaterialAssetPaths.insert(normalizedMaterialPath);
        if (inserted)
            VX_ENGINE_ERROR_STREAM("Failed to load material override asset: " << normalizedMaterialPath << '\n');
        return nullptr;
    }

    Material::SharedPtr material;
    try
    {
        material = createMaterialFromCpuData(materialAsset->material, std::filesystem::path(normalizedMaterialPath));
    }
    catch (const std::exception &e)
    {
        VX_ENGINE_ERROR_STREAM("Exception creating material from '" << normalizedMaterialPath << "': " << e.what() << '\n');
        return nullptr;
    }

    if (!material)
        return nullptr;

    m_failedMaterialAssetPaths.erase(normalizedMaterialPath);
    m_materialsByAssetPath[normalizedMaterialPath] = material;
    return material;
}

Material::SharedPtr SceneMaterialResolver::resolveRuntimeMeshMaterial(const CPUMesh &mesh)
{
    if (mesh.material.albedoTexture.empty())
        return Material::getDefaultMaterial();

    const std::string materialCacheKey = buildMaterialCacheKey(mesh.material);

    if (auto materialIt = m_materialsByRuntimeKey.find(materialCacheKey); materialIt != m_materialsByRuntimeKey.end())
        return materialIt->second;

    if (m_failedRuntimeMaterialKeys.find(materialCacheKey) != m_failedRuntimeMaterialKeys.end())
        return Material::getDefaultMaterial();

    if (!consumeLoadBudget())
        return Material::getDefaultMaterial();

    Material::SharedPtr material;
    try
    {
        material = createMaterialFromCpuData(mesh.material, {});
    }
    catch (const std::exception &e)
    {
        VX_ENGINE_ERROR_STREAM("Exception creating material for mesh '" << mesh.name << "': " << e.what() << '\n');
    }

    if (!material)
    {
        const auto [_, inserted] = m_failedRuntimeMaterialKeys.insert(materialCacheKey);
        if (inserted)
        {
            VX_ENGINE_ERROR_STREAM("Failed to create runtime mesh material for mesh: " << mesh.name << '\n');
            VX_ENGINE_WARNING_STREAM("Using default material for unresolved runtime mesh material (cached to avoid per-frame reload attempts)\n");
        }

        auto fallbackMaterial = Material::getDefaultMaterial();
        m_materialsByRuntimeKey[materialCacheKey] = fallbackMaterial;
        return fallbackMaterial;
    }

    m_failedRuntimeMaterialKeys.erase(materialCacheKey);
    m_materialsByRuntimeKey[materialCacheKey] = material;
    return material;
}

bool SceneMaterialResolver::looksLikeWindowsAbsolutePath(const std::string &path) const
{
    return path.size() >= 3u &&
           std::isalpha(static_cast<unsigned char>(path[0])) &&
           path[1] == ':' &&
           (path[2] == '\\' || path[2] == '/');
}

std::filesystem::path SceneMaterialResolver::makeAbsoluteNormalized(const std::filesystem::path &path) const
{
    std::error_code errorCode;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
    if (errorCode)
        return path.lexically_normal();

    return absolutePath.lexically_normal();
}

std::string SceneMaterialResolver::resolveTexturePathForMaterial(const std::string &texturePath,
                                                                 const std::filesystem::path &materialAssetPath) const
{
    if (texturePath.empty())
        return {};

    if (looksLikeWindowsAbsolutePath(texturePath))
        return texturePath;

    const std::filesystem::path parsedPath(texturePath);
    if (parsedPath.is_absolute())
        return makeAbsoluteNormalized(parsedPath).string();

    if (!materialAssetPath.empty())
    {
        const std::filesystem::path materialDirectory = makeAbsoluteNormalized(materialAssetPath.parent_path());

        const std::filesystem::path materialRelativePath = makeAbsoluteNormalized(materialDirectory / parsedPath);
        if (fileExists(materialRelativePath))
            return materialRelativePath.string();

        std::filesystem::path probeDirectory = materialDirectory;
        while (!probeDirectory.empty())
        {
            const std::filesystem::path ancestorRelativePath = makeAbsoluteNormalized(probeDirectory / parsedPath);
            if (fileExists(ancestorRelativePath))
                return ancestorRelativePath.string();

            if (!probeDirectory.has_parent_path())
                break;

            const std::filesystem::path parentDirectory = probeDirectory.parent_path();
            if (parentDirectory == probeDirectory)
                break;

            probeDirectory = parentDirectory;
        }
    }

    return makeAbsoluteNormalized(parsedPath).string();
}

Texture::SharedPtr SceneMaterialResolver::loadTextureForMaterial(const std::string &texturePath,
                                                                 VkFormat format,
                                                                 const std::filesystem::path &materialAssetPath)
{
    if (texturePath.empty())
        return nullptr;

    std::vector<std::string> candidates;
    candidates.reserve(2u);

    const std::string resolvedTexturePath = resolveTexturePathForMaterial(texturePath, materialAssetPath);
    if (!resolvedTexturePath.empty())
        candidates.push_back(resolvedTexturePath);

    if (candidates.empty() || candidates.front() != texturePath)
        candidates.push_back(texturePath);

    for (const auto &candidatePath : candidates)
    {
        const std::string cacheKey = candidatePath + "|" + std::to_string(static_cast<uint32_t>(format));
        if (auto textureIt = m_texturesByResolvedPath.find(cacheKey); textureIt != m_texturesByResolvedPath.end())
            return textureIt->second;

        if (m_failedTextureResolvedPaths.find(cacheKey) != m_failedTextureResolvedPaths.end())
            continue;

        auto texture = AssetsLoader::loadTextureGPU(candidatePath, format);
        if (texture)
        {
            m_texturesByResolvedPath[cacheKey] = texture;
            m_failedTextureResolvedPaths.erase(cacheKey);
            return texture;
        }

        m_failedTextureResolvedPaths.insert(cacheKey);
    }

    return nullptr;
}

std::string SceneMaterialResolver::buildMaterialCacheKey(const CPUMaterial &materialCPU) const
{
    size_t seed = 0u;
    hashing::hashCombine(seed, materialCPU.name);
    hashing::hashCombine(seed, materialCPU.albedoTexture);
    hashing::hashCombine(seed, materialCPU.normalTexture);
    hashing::hashCombine(seed, materialCPU.ormTexture);
    hashing::hashCombine(seed, materialCPU.emissiveTexture);
    hashing::hashCombine(seed, materialCPU.flags);
    hashing::hashCombine(seed, hashFloat(materialCPU.baseColorFactor.x));
    hashing::hashCombine(seed, hashFloat(materialCPU.baseColorFactor.y));
    hashing::hashCombine(seed, hashFloat(materialCPU.baseColorFactor.z));
    hashing::hashCombine(seed, hashFloat(materialCPU.baseColorFactor.w));
    hashing::hashCombine(seed, hashFloat(materialCPU.emissiveFactor.x));
    hashing::hashCombine(seed, hashFloat(materialCPU.emissiveFactor.y));
    hashing::hashCombine(seed, hashFloat(materialCPU.emissiveFactor.z));
    hashing::hashCombine(seed, hashFloat(materialCPU.metallicFactor));
    hashing::hashCombine(seed, hashFloat(materialCPU.roughnessFactor));
    hashing::hashCombine(seed, hashFloat(materialCPU.aoStrength));
    hashing::hashCombine(seed, hashFloat(materialCPU.normalScale));
    hashing::hashCombine(seed, hashFloat(materialCPU.alphaCutoff));
    hashing::hashCombine(seed, hashFloat(materialCPU.ior));
    hashing::hashCombine(seed, hashFloat(materialCPU.uvScale.x));
    hashing::hashCombine(seed, hashFloat(materialCPU.uvScale.y));
    hashing::hashCombine(seed, hashFloat(materialCPU.uvOffset.x));
    hashing::hashCombine(seed, hashFloat(materialCPU.uvOffset.y));
    hashing::hashCombine(seed, hashFloat(materialCPU.uvRotation));
    hashing::hashCombine(seed, materialCPU.customExpression);
    hashing::hashCombine(seed, materialCPU.customShaderHash);

    for (const auto &noiseNode : materialCPU.noiseNodes)
    {
        hashing::hashCombine(seed, static_cast<uint8_t>(noiseNode.type));
        hashing::hashCombine(seed, static_cast<uint8_t>(noiseNode.blendMode));
        hashing::hashCombine(seed, hashFloat(noiseNode.scale));
        hashing::hashCombine(seed, noiseNode.octaves);
        hashing::hashCombine(seed, hashFloat(noiseNode.persistence));
        hashing::hashCombine(seed, hashFloat(noiseNode.lacunarity));
        hashing::hashCombine(seed, noiseNode.worldSpace);
        hashing::hashCombine(seed, noiseNode.active);
        hashing::hashCombine(seed, noiseNode.targetSlot);
        hashing::hashCombine(seed, hashFloat(noiseNode.rampColorA.x));
        hashing::hashCombine(seed, hashFloat(noiseNode.rampColorA.y));
        hashing::hashCombine(seed, hashFloat(noiseNode.rampColorA.z));
        hashing::hashCombine(seed, hashFloat(noiseNode.rampColorB.x));
        hashing::hashCombine(seed, hashFloat(noiseNode.rampColorB.y));
        hashing::hashCombine(seed, hashFloat(noiseNode.rampColorB.z));
    }

    for (const auto &colorNode : materialCPU.colorNodes)
    {
        hashing::hashCombine(seed, static_cast<uint8_t>(colorNode.blendMode));
        hashing::hashCombine(seed, hashFloat(colorNode.color.x));
        hashing::hashCombine(seed, hashFloat(colorNode.color.y));
        hashing::hashCombine(seed, hashFloat(colorNode.color.z));
        hashing::hashCombine(seed, hashFloat(colorNode.strength));
        hashing::hashCombine(seed, colorNode.active);
        hashing::hashCombine(seed, colorNode.targetSlot);
    }

    return std::to_string(seed);
}

std::string SceneMaterialResolver::ensureCustomMaterialShaderPath(const CPUMaterial &materialCPU) const
{
    if (!materialCPU.customShaderHash.empty())
    {
        const std::string hashedShaderPath = "./resources/shaders/material_cache/" + materialCPU.customShaderHash + ".spv";
        std::error_code existsError;
        if (std::filesystem::exists(hashedShaderPath, existsError) && !existsError)
            return hashedShaderPath;
    }

    if (materialCPU.customExpression.empty())
        return {};

    std::ifstream templateFile("./resources/shaders/gbuffer_static_template.frag_template");
    if (!templateFile.is_open())
    {
        VX_ENGINE_WARNING_STREAM("Failed to open custom material shader template. Falling back to default material shader.\n");
        return {};
    }

    std::stringstream templateBuffer;
    templateBuffer << templateFile.rdbuf();
    std::string templateSrc = templateBuffer.str();

    const std::string funcMarker = "// [FUNCTIONS]\n";
    const std::string exprMarker = "// [EXPRESSION]\n";
    std::string functions;
    std::string expression = materialCPU.customExpression;

    const size_t functionsPos = materialCPU.customExpression.find(funcMarker);
    const size_t expressionPos = materialCPU.customExpression.find(exprMarker);
    if (functionsPos != std::string::npos && expressionPos != std::string::npos && expressionPos >= functionsPos + funcMarker.size())
    {
        functions = materialCPU.customExpression.substr(functionsPos + funcMarker.size(), expressionPos - functionsPos - funcMarker.size());
        expression = materialCPU.customExpression.substr(expressionPos + exprMarker.size());
    }

    auto replaceFirst = [](std::string &textValue, const std::string &from, const std::string &to)
    {
        const size_t pos = textValue.find(from);
        if (pos != std::string::npos)
            textValue.replace(pos, from.size(), to);
    };

    replaceFirst(templateSrc, "// <<ELIX_CUSTOM_FUNCTIONS>>", functions);
    replaceFirst(templateSrc, "// <<ELIX_CUSTOM_EXPRESSION>>", expression);

    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    uint64_t hash = kFnvOffset;
    const std::string hashInput = std::string("gbuffer_bindless_layout_v1\n") + functions + "\n" + expression;
    for (unsigned char c : hashInput)
    {
        hash ^= c;
        hash *= kFnvPrime;
    }

    std::error_code errorCode;
    std::filesystem::create_directories("./resources/shaders/material_cache", errorCode);
    const std::string spvPath = "./resources/shaders/material_cache/" + std::to_string(hash) + ".spv";

    if (!std::filesystem::exists(spvPath))
    {
        try
        {
            const auto spirv = shaders::ShaderCompiler::compileGLSL(
                templateSrc, shaderc_glsl_fragment_shader, shaders::ShaderCompiler::ShaderCompilerFlagBits::EDEFAULT, "custom_material.frag");

            std::ofstream outputFile(spvPath, std::ios::binary | std::ios::trunc);
            if (!outputFile.is_open())
            {
                VX_ENGINE_WARNING_STREAM("Failed to open custom material shader cache file: " << spvPath << "\n");
                return {};
            }

            outputFile.write(reinterpret_cast<const char *>(spirv.data()), static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
            outputFile.close();
            if (!outputFile.good())
            {
                VX_ENGINE_WARNING_STREAM("Failed to write custom material shader cache file: " << spvPath << "\n");
                return {};
            }
        }
        catch (const std::exception &exception)
        {
            VX_ENGINE_WARNING_STREAM("Failed to compile custom material shader: " << exception.what() << "\n");
            return {};
        }
    }

    return spvPath;
}

Material::SharedPtr SceneMaterialResolver::createMaterialFromCpuData(const CPUMaterial &materialCPU,
                                                                     const std::filesystem::path &materialAssetFilePath)
{
    auto albedoTexture = loadTextureForMaterial(materialCPU.albedoTexture, VK_FORMAT_R8G8B8A8_SRGB, materialAssetFilePath);
    auto normalTexture = loadTextureForMaterial(materialCPU.normalTexture, VK_FORMAT_R8G8B8A8_UNORM, materialAssetFilePath);
    auto ormTexture = loadTextureForMaterial(materialCPU.ormTexture, VK_FORMAT_R8G8B8A8_UNORM, materialAssetFilePath);
    auto emissiveTexture = loadTextureForMaterial(materialCPU.emissiveTexture, VK_FORMAT_R8G8B8A8_SRGB, materialAssetFilePath);

    if (!albedoTexture)
        albedoTexture = Texture::getDefaultWhiteTexture();
    if (!normalTexture)
        normalTexture = Texture::getDefaultNormalTexture();
    if (!ormTexture)
        ormTexture = Texture::getDefaultOrmTexture();
    if (!emissiveTexture)
        emissiveTexture = Texture::getDefaultBlackTexture();

    auto material = Material::create(albedoTexture);
    if (!material)
        return nullptr;

    material->setAlbedoTexture(albedoTexture);
    material->setNormalTexture(normalTexture);
    material->setOrmTexture(ormTexture);
    material->setEmissiveTexture(emissiveTexture);
    material->setBaseColorFactor(materialCPU.baseColorFactor);
    material->setEmissiveFactor(materialCPU.emissiveFactor);
    material->setMetallic(materialCPU.metallicFactor);
    material->setRoughness(materialCPU.roughnessFactor);
    material->setAoStrength(materialCPU.aoStrength);
    material->setNormalScale(materialCPU.normalScale);
    material->setAlphaCutoff(materialCPU.alphaCutoff);
    material->setFlags(materialCPU.flags);
    material->setUVScale(materialCPU.uvScale);
    material->setUVOffset(materialCPU.uvOffset);
    material->setUVRotation(materialCPU.uvRotation);
    material->setIor(materialCPU.ior);

    if (const std::string customFragPath = ensureCustomMaterialShaderPath(materialCPU); !customFragPath.empty())
        material->setCustomFragPath(customFragPath);

    return material;
}

bool SceneMaterialResolver::consumeLoadBudget()
{
    if (m_newMaterialLoadsThisFrame >= m_maxNewMaterialLoadsPerFrame)
        return false;

    ++m_newMaterialLoadsThisFrame;
    return true;
}

ELIX_NESTED_NAMESPACE_END
