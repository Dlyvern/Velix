#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Render/ObjectIdEncoding.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"

#include <iostream>
#include <array>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_set>
#include <limits>
#include <cmath>
#include <cfloat>
#include <exception>
#include <filesystem>

#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/TerrainComponent.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Assets/AssetsLoader.hpp"

#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Utilities/ImageUtilities.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Threads/ThreadPoolManager.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct CameraUBO
{
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 invView;
    glm::mat4 invProjection;
};

struct LightData
{
    glm::vec4 position;
    glm::vec4 direction;
    glm::vec4 colorStrength;
    glm::vec4 parameters;
    glm::vec4 shadowInfo; // x = casts shadow, y = shadow index, z = far/range, w = near
};

struct LightSSBO
{
    int lightCount;
    glm::vec3 padding{0.0f};
    LightData lights[];
};

struct LightSpaceMatrixUBO
{
    glm::mat4 lightSpaceMatrix{1.0f};
    std::array<glm::mat4, elix::engine::ShadowConstants::MAX_DIRECTIONAL_CASCADES> directionalLightSpaceMatrices;
    glm::vec4 directionalCascadeSplits{glm::vec4(std::numeric_limits<float>::max())};
    std::array<glm::mat4, elix::engine::ShadowConstants::MAX_SPOT_SHADOWS> spotLightSpaceMatrices;

    LightSpaceMatrixUBO()
    {
        directionalLightSpaceMatrices.fill(1.0f);
        spotLightSpaceMatrices.fill(1.0f);
    }
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

RenderGraph::RenderGraph(bool presentToSwapchain) : m_presentToSwapchain(presentToSwapchain)
{
    m_device = core::VulkanContext::getContext()->getDevice();
    m_swapchain = core::VulkanContext::getContext()->getSwapchain();

    m_commandBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
    m_secondaryCommandPools.resize(MAX_FRAMES_IN_FLIGHT);
    m_secondaryCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_commandPools.reserve(MAX_FRAMES_IN_FLIGHT);

    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled
    fenceInfo.pNext = nullptr;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create renderFinished semaphore for image!");
    }
}

void RenderGraph::prepareFrameDataFromScene(Scene *scene, const glm::mat4 &view, const glm::mat4 &projection, bool enableFrustumCulling)
{
    (void)projection;
    (void)enableFrustumCulling;

    const auto &sceneEntities = scene->getEntities();

    static std::size_t lastEntitiesSize = 0;
    const std::size_t entitiesSize = sceneEntities.size();

    if (entitiesSize != lastEntitiesSize)
    {
        if (entitiesSize < lastEntitiesSize)
            VX_ENGINE_INFO_STREAM("Entity was deleted");
        else if (entitiesSize > lastEntitiesSize)
            VX_ENGINE_INFO_STREAM("Entity was addded");

        lastEntitiesSize = entitiesSize;
    }

    std::unordered_set<Entity *> sceneEntitySet;
    sceneEntitySet.reserve(sceneEntities.size());
    for (const auto &e : sceneEntities)
        if (e)
            sceneEntitySet.insert(e.get());

    for (auto it = m_perFrameData.drawItems.begin(); it != m_perFrameData.drawItems.end();)
    {
        if (sceneEntitySet.find(it->first) == sceneEntitySet.end())
            it = m_perFrameData.drawItems.erase(it);
        else
            ++it;
    }

    auto computeMeshGeometryHash = [](const CPUMesh &mesh) -> std::size_t
    {
        std::size_t hashData{0};
        hashing::hash(hashData, mesh.vertexStride);
        hashing::hash(hashData, mesh.vertexLayoutHash);

        for (const auto &vertexByte : mesh.vertexData)
            hashing::hash(hashData, vertexByte);

        for (const auto &index : mesh.indices)
            hashing::hash(hashData, index);

        return hashData;
    };

    auto computeMeshLocalBounds = [&](const CPUMesh &mesh) -> MeshLocalBounds
    {
        MeshLocalBounds bounds{};
        if (mesh.vertexStride < sizeof(glm::vec3) || mesh.vertexData.empty())
            return bounds;

        const uint32_t vertexCount = static_cast<uint32_t>(mesh.vertexData.size() / mesh.vertexStride);
        if (vertexCount == 0)
            return bounds;

        glm::vec3 minPosition(FLT_MAX);
        glm::vec3 maxPosition(-FLT_MAX);

        for (uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            glm::vec3 position(0.0f);
            std::memcpy(&position, mesh.vertexData.data() + static_cast<size_t>(vertexIndex) * mesh.vertexStride, sizeof(glm::vec3));
            minPosition = glm::min(minPosition, position);
            maxPosition = glm::max(maxPosition, position);
        }

        bounds.center = (minPosition + maxPosition) * 0.5f;
        bounds.radius = 0.0f;

        for (uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
        {
            glm::vec3 position(0.0f);
            std::memcpy(&position, mesh.vertexData.data() + static_cast<size_t>(vertexIndex) * mesh.vertexStride, sizeof(glm::vec3));
            bounds.radius = std::max(bounds.radius, glm::length(position - bounds.center));
        }

        return bounds;
    };

    auto getOrCreateSharedGeometryMesh = [&](const CPUMesh &mesh) -> GPUMesh::SharedPtr
    {
        const std::size_t hashData = computeMeshGeometryHash(mesh);

        auto meshIt = m_meshes.find(hashData);
        if (meshIt != m_meshes.end())
            return meshIt->second;

        auto createdMesh = GPUMesh::createFromMesh(mesh);
        m_meshes[hashData] = createdMesh;
        m_meshLocalBoundsByHash[hashData] = computeMeshLocalBounds(mesh);
        return createdMesh;
    };

    auto createDrawMeshInstance = [&](const CPUMesh &mesh) -> GPUMesh::SharedPtr
    {
        auto sharedGeometry = getOrCreateSharedGeometryMesh(mesh);
        if (!sharedGeometry)
            return nullptr;

        auto instance = std::make_shared<GPUMesh>();
        instance->indexBuffer = sharedGeometry->indexBuffer;
        instance->vertexBuffer = sharedGeometry->vertexBuffer;
        instance->rayTracingIndexBuffer = sharedGeometry->rayTracingIndexBuffer;
        instance->rayTracingVertexBuffer = sharedGeometry->rayTracingVertexBuffer;
        instance->indicesCount = sharedGeometry->indicesCount;
        instance->indexType = sharedGeometry->indexType;
        instance->vertexStride = sharedGeometry->vertexStride;
        instance->vertexLayoutHash = sharedGeometry->vertexLayoutHash;
        instance->rayTracingPositionOffset = sharedGeometry->rayTracingPositionOffset;
        instance->rayTracingPositionFormat = sharedGeometry->rayTracingPositionFormat;

        return instance;
    };

    auto looksLikeWindowsAbsolutePath = [](const std::string &path) -> bool
    {
        return path.size() >= 3u &&
               std::isalpha(static_cast<unsigned char>(path[0])) &&
               path[1] == ':' &&
               (path[2] == '\\' || path[2] == '/');
    };

    auto makeAbsoluteNormalized = [](const std::filesystem::path &path) -> std::filesystem::path
    {
        std::error_code errorCode;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
        if (errorCode)
            return path.lexically_normal();

        return absolutePath.lexically_normal();
    };

    auto resolveTexturePathForMaterial = [&](const std::string &texturePath, const std::filesystem::path &materialAssetPath) -> std::string
    {
        if (texturePath.empty())
            return {};

        if (looksLikeWindowsAbsolutePath(texturePath))
            return texturePath;

        auto fileExists = [](const std::filesystem::path &path) -> bool
        {
            std::error_code errorCode;
            return std::filesystem::exists(path, errorCode) && !errorCode;
        };

        const std::filesystem::path parsedPath(texturePath);
        if (parsedPath.is_absolute())
            return makeAbsoluteNormalized(parsedPath).string();

        if (!materialAssetPath.empty())
        {
            const std::filesystem::path materialDirectory = makeAbsoluteNormalized(materialAssetPath.parent_path());

            const std::filesystem::path materialRelativePath = makeAbsoluteNormalized(materialDirectory / parsedPath);
            if (fileExists(materialRelativePath))
                return materialRelativePath.string();

            // Support project-relative paths serialized in materials (for example:
            // "Office.model_Materials/Foo.tex.elixasset") by probing parent
            // directories of the material file.
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

        const std::filesystem::path cwdRelativePath = makeAbsoluteNormalized(parsedPath);
        return cwdRelativePath.string();
    };

    auto loadTextureForMaterial = [&](const std::string &texturePath, VkFormat format, const std::filesystem::path &materialAssetPath) -> Texture::SharedPtr
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
    };

    auto hashFloat = [](float value) -> size_t
    {
        if (!std::isfinite(value))
            return 0u;

        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(float));
        return std::hash<uint32_t>()(bits);
    };

    auto hashCombine = [](size_t &seed, size_t value)
    {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    };

    auto buildMaterialCacheKey = [&](const CPUMaterial &materialCPU) -> std::string
    {
        size_t seed = 0u;
        hashCombine(seed, std::hash<std::string>()(materialCPU.name));
        hashCombine(seed, std::hash<std::string>()(materialCPU.albedoTexture));
        hashCombine(seed, std::hash<std::string>()(materialCPU.normalTexture));
        hashCombine(seed, std::hash<std::string>()(materialCPU.ormTexture));
        hashCombine(seed, std::hash<std::string>()(materialCPU.emissiveTexture));
        hashCombine(seed, std::hash<uint32_t>()(materialCPU.flags));
        hashCombine(seed, hashFloat(materialCPU.baseColorFactor.x));
        hashCombine(seed, hashFloat(materialCPU.baseColorFactor.y));
        hashCombine(seed, hashFloat(materialCPU.baseColorFactor.z));
        hashCombine(seed, hashFloat(materialCPU.baseColorFactor.w));
        hashCombine(seed, hashFloat(materialCPU.emissiveFactor.x));
        hashCombine(seed, hashFloat(materialCPU.emissiveFactor.y));
        hashCombine(seed, hashFloat(materialCPU.emissiveFactor.z));
        hashCombine(seed, hashFloat(materialCPU.metallicFactor));
        hashCombine(seed, hashFloat(materialCPU.roughnessFactor));
        hashCombine(seed, hashFloat(materialCPU.aoStrength));
        hashCombine(seed, hashFloat(materialCPU.normalScale));
        hashCombine(seed, hashFloat(materialCPU.alphaCutoff));
        hashCombine(seed, hashFloat(materialCPU.ior));
        hashCombine(seed, hashFloat(materialCPU.uvScale.x));
        hashCombine(seed, hashFloat(materialCPU.uvScale.y));
        hashCombine(seed, hashFloat(materialCPU.uvOffset.x));
        hashCombine(seed, hashFloat(materialCPU.uvOffset.y));
        hashCombine(seed, hashFloat(materialCPU.uvRotation));
        return std::to_string(seed);
    };

    auto createMaterialFromCpuData = [&](const CPUMaterial &materialCPU, const std::filesystem::path &materialAssetFilePath) -> Material::SharedPtr
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
        return material;
    };

    auto resolveMaterialOverrideFromPath = [&](const std::string &materialPath) -> Material::SharedPtr
    {
        if (materialPath.empty())
            return nullptr;

        std::string normalizedMaterialPath = materialPath;
        if (!looksLikeWindowsAbsolutePath(materialPath))
            normalizedMaterialPath = makeAbsoluteNormalized(std::filesystem::path(materialPath)).string();

        auto cachedMaterialIt = m_materialsByAssetPath.find(normalizedMaterialPath);
        if (cachedMaterialIt != m_materialsByAssetPath.end())
            return cachedMaterialIt->second;

        if (m_failedMaterialAssetPaths.find(normalizedMaterialPath) != m_failedMaterialAssetPaths.end())
            return nullptr;

        auto materialAsset = AssetsLoader::loadMaterial(normalizedMaterialPath);
        if (!materialAsset.has_value())
        {
            const auto [_, inserted] = m_failedMaterialAssetPaths.insert(normalizedMaterialPath);
            if (inserted)
                VX_ENGINE_ERROR_STREAM("Failed to load material override asset: " << normalizedMaterialPath << '\n');
            return nullptr;
        }

        auto materialCPU = materialAsset.value().material;
        const std::filesystem::path materialAssetFilePath = std::filesystem::path(normalizedMaterialPath);
        auto material = createMaterialFromCpuData(materialCPU, materialAssetFilePath);
        if (!material)
            return nullptr;

        m_failedMaterialAssetPaths.erase(normalizedMaterialPath);
        m_materialsByAssetPath[normalizedMaterialPath] = material;
        return material;
    };

    auto resolveMeshMaterial = [&](const CPUMesh &mesh,
                                   StaticMeshComponent *staticComponent,
                                   SkeletalMeshComponent *skeletalComponent,
                                   TerrainComponent *terrainComponent,
                                   size_t slot) -> Material::SharedPtr
    {
        if (staticComponent)
        {
            auto overrideMaterial = staticComponent->getMaterialOverride(slot);
            if (!overrideMaterial)
            {
                const std::string &overridePath = staticComponent->getMaterialOverridePath(slot);
                if (!overridePath.empty())
                {
                    overrideMaterial = resolveMaterialOverrideFromPath(overridePath);
                    if (overrideMaterial)
                        staticComponent->setMaterialOverride(slot, overrideMaterial);
                }
            }

            if (overrideMaterial)
                return overrideMaterial;
        }
        else if (skeletalComponent)
        {
            auto overrideMaterial = skeletalComponent->getMaterialOverride(slot);
            if (!overrideMaterial)
            {
                const std::string &overridePath = skeletalComponent->getMaterialOverridePath(slot);
                if (!overridePath.empty())
                {
                    overrideMaterial = resolveMaterialOverrideFromPath(overridePath);
                    if (overrideMaterial)
                        skeletalComponent->setMaterialOverride(slot, overrideMaterial);
                }
            }

            if (overrideMaterial)
                return overrideMaterial;
        }
        else if (terrainComponent)
        {
            const std::string &overridePath = terrainComponent->getMaterialOverridePath();
            if (!overridePath.empty())
            {
                auto overrideMaterial = resolveMaterialOverrideFromPath(overridePath);
                if (overrideMaterial)
                    return overrideMaterial;
            }
        }

        if (mesh.material.albedoTexture.empty())
            return Material::getDefaultMaterial();

        const std::string materialCacheKey = buildMaterialCacheKey(mesh.material);

        auto materialIt = m_materialsByAlbedoPath.find(materialCacheKey);
        if (materialIt != m_materialsByAlbedoPath.end())
            return materialIt->second;

        if (m_failedAlbedoTexturePaths.find(materialCacheKey) != m_failedAlbedoTexturePaths.end())
            return Material::getDefaultMaterial();

        auto material = createMaterialFromCpuData(mesh.material, {});
        if (!material)
        {
            const auto [_, inserted] = m_failedAlbedoTexturePaths.insert(materialCacheKey);
            if (inserted)
            {
                VX_ENGINE_ERROR_STREAM("Failed to create runtime mesh material for mesh: " << mesh.name << '\n');
                VX_ENGINE_WARNING_STREAM("Using default material for unresolved runtime mesh material (cached to avoid per-frame reload attempts)\n");
            }

            auto fallbackMaterial = Material::getDefaultMaterial();
            m_materialsByAlbedoPath[materialCacheKey] = fallbackMaterial;
            return fallbackMaterial;
        }

        m_failedAlbedoTexturePaths.erase(materialCacheKey);
        m_materialsByAlbedoPath[materialCacheKey] = material;
        return material;
    };

    auto updateDrawItemBones = [](DrawItem &drawItem, Entity::SharedPtr entity)
    {
        if (auto skeletalComponent = entity->getComponent<SkeletalMeshComponent>())
        {
            auto &skeleton = skeletalComponent->getSkeleton();
            if (auto animator = entity->getComponent<AnimatorComponent>(); animator && animator->isAnimationPlaying())
                drawItem.finalBones = skeleton.getFinalMatrices();
            else
                drawItem.finalBones = skeleton.getBindPoses();
        }
        else
            drawItem.finalBones.clear();
    };

    const glm::vec3 cameraPosition = glm::vec3(glm::inverse(view)[3]);

    for (const auto &entity : sceneEntities)
    {
        if (!entity || !entity->isEnabled())
        {
            auto drawItemIt = m_perFrameData.drawItems.find(entity.get());
            if (drawItemIt != m_perFrameData.drawItems.end())
                m_perFrameData.drawItems.erase(drawItemIt);
            continue;
        }

        auto staticMeshComponent = entity->getComponent<StaticMeshComponent>();
        auto skeletalMeshComponent = entity->getComponent<SkeletalMeshComponent>();
        auto terrainComponent = entity->getComponent<TerrainComponent>();

        const std::vector<CPUMesh> *meshes = nullptr;
        if (staticMeshComponent)
            meshes = &staticMeshComponent->getMeshes();
        else if (skeletalMeshComponent)
            meshes = &skeletalMeshComponent->getMeshes();
        else if (terrainComponent)
        {
            terrainComponent->ensureChunkMeshesBuilt();
            meshes = &terrainComponent->getChunkMeshes();
        }

        if (!meshes || meshes->empty())
        {
            auto drawItemIt = m_perFrameData.drawItems.find(entity.get());
            if (drawItemIt != m_perFrameData.drawItems.end())
                m_perFrameData.drawItems.erase(drawItemIt);
            continue;
        }

        auto drawItemIt = m_perFrameData.drawItems.find(entity.get());
        if (drawItemIt == m_perFrameData.drawItems.end())
            drawItemIt = m_perFrameData.drawItems.emplace(entity.get(), DrawItem{}).first;

        auto &drawItem = drawItemIt->second;
        drawItem.transform = entity->hasComponent<Transform3DComponent>() ? entity->getComponent<Transform3DComponent>()->getMatrix() : glm::mat4(1.0f);
        drawItem.bonesOffset = 0;
        updateDrawItemBones(drawItem, entity);

        if (drawItem.meshes.size() != meshes->size())
        {
            drawItem.meshes.clear();
            drawItem.meshes.reserve(meshes->size());
            drawItem.localMeshGeometryHashes.clear();
            drawItem.localMeshGeometryHashes.reserve(meshes->size());
            drawItem.localMeshBoundsCenters.clear();
            drawItem.localMeshBoundsCenters.reserve(meshes->size());
            drawItem.localMeshBoundsRadii.clear();
            drawItem.localMeshBoundsRadii.reserve(meshes->size());
            drawItem.localMeshTransforms.clear();
            drawItem.localMeshTransforms.reserve(meshes->size());

            for (const auto &mesh : *meshes)
            {
                const std::size_t hashData = computeMeshGeometryHash(mesh);
                drawItem.meshes.push_back(createDrawMeshInstance(mesh));
                drawItem.localMeshGeometryHashes.push_back(hashData);
                drawItem.localMeshTransforms.push_back(mesh.localTransform);

                const auto boundsIt = m_meshLocalBoundsByHash.find(hashData);
                if (boundsIt != m_meshLocalBoundsByHash.end())
                {
                    drawItem.localMeshBoundsCenters.push_back(boundsIt->second.center);
                    drawItem.localMeshBoundsRadii.push_back(boundsIt->second.radius);
                }
                else
                {
                    drawItem.localMeshBoundsCenters.push_back(glm::vec3(0.0f));
                    drawItem.localMeshBoundsRadii.push_back(0.0f);
                }
            }
        }

        for (size_t meshIndex = 0; meshIndex < meshes->size() && meshIndex < drawItem.meshes.size(); ++meshIndex)
        {
            if (meshIndex >= drawItem.localMeshTransforms.size())
                drawItem.localMeshTransforms.resize(meshes->size(), glm::mat4(1.0f));
            if (meshIndex >= drawItem.localMeshGeometryHashes.size())
                drawItem.localMeshGeometryHashes.resize(meshes->size(), 0u);
            if (meshIndex >= drawItem.localMeshBoundsCenters.size())
                drawItem.localMeshBoundsCenters.resize(meshes->size(), glm::vec3(0.0f));
            if (meshIndex >= drawItem.localMeshBoundsRadii.size())
                drawItem.localMeshBoundsRadii.resize(meshes->size(), 0.0f);

            const CPUMesh &sourceMesh = (*meshes)[meshIndex];
            const std::size_t currentGeometryHash = computeMeshGeometryHash(sourceMesh);
            if (!drawItem.meshes[meshIndex] ||
                drawItem.localMeshGeometryHashes[meshIndex] != currentGeometryHash)
            {
                drawItem.meshes[meshIndex] = createDrawMeshInstance(sourceMesh);
                drawItem.localMeshGeometryHashes[meshIndex] = currentGeometryHash;

                const auto boundsIt = m_meshLocalBoundsByHash.find(currentGeometryHash);
                if (boundsIt != m_meshLocalBoundsByHash.end())
                {
                    drawItem.localMeshBoundsCenters[meshIndex] = boundsIt->second.center;
                    drawItem.localMeshBoundsRadii[meshIndex] = boundsIt->second.radius;
                }
                else
                {
                    drawItem.localMeshBoundsCenters[meshIndex] = glm::vec3(0.0f);
                    drawItem.localMeshBoundsRadii[meshIndex] = 0.0f;
                }
            }

            glm::mat4 meshLocalTransform = sourceMesh.localTransform;

            if (skeletalMeshComponent && sourceMesh.attachedBoneId >= 0)
            {
                auto &skeleton = skeletalMeshComponent->getSkeleton();
                if (auto *attachmentBone = skeleton.getBone(sourceMesh.attachedBoneId))
                {
                    meshLocalTransform = attachmentBone->finalTransformation * meshLocalTransform;
                }
            }

            drawItem.localMeshTransforms[meshIndex] = meshLocalTransform;

            if (!drawItem.meshes[meshIndex])
                continue;

            drawItem.meshes[meshIndex]->material =
                resolveMeshMaterial((*meshes)[meshIndex], staticMeshComponent, skeletalMeshComponent, terrainComponent, meshIndex);
        }

        glm::vec3 boundsMin(FLT_MAX);
        glm::vec3 boundsMax(-FLT_MAX);
        bool hasBounds = false;

        const size_t meshCount = drawItem.localMeshBoundsRadii.size();
        drawItem.cachedWorldBoundsCenters.resize(meshCount);
        drawItem.cachedWorldBoundsRadii.resize(meshCount);

        for (size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
        {
            const glm::mat4 localTransform = meshIndex < drawItem.localMeshTransforms.size()
                                                 ? drawItem.localMeshTransforms[meshIndex]
                                                 : glm::mat4(1.0f);
            const glm::mat4 meshModel = drawItem.transform * localTransform;

            const glm::vec3 worldCenter = glm::vec3(meshModel * glm::vec4(drawItem.localMeshBoundsCenters[meshIndex], 1.0f));

            const float scaleX = glm::length(glm::vec3(meshModel[0]));
            const float scaleY = glm::length(glm::vec3(meshModel[1]));
            const float scaleZ = glm::length(glm::vec3(meshModel[2]));
            const float maxScale = std::max({scaleX, scaleY, scaleZ, 1.0f});
            const float worldRadius = drawItem.localMeshBoundsRadii[meshIndex] * maxScale;

            drawItem.cachedWorldBoundsCenters[meshIndex] = worldCenter;
            drawItem.cachedWorldBoundsRadii[meshIndex] = worldRadius;

            const glm::vec3 extents(worldRadius);
            boundsMin = glm::min(boundsMin, worldCenter - extents);
            boundsMax = glm::max(boundsMax, worldCenter + extents);
            hasBounds = true;
        }

        (void)hasBounds;
    }

    std::vector<glm::mat4> frameBones;
    frameBones.reserve(1024);

    for (auto &[_, drawItem] : m_perFrameData.drawItems)
    {
        drawItem.bonesOffset = 0;

        if (drawItem.finalBones.empty())
            continue;

        drawItem.bonesOffset = static_cast<uint32_t>(frameBones.size());
        frameBones.insert(frameBones.end(), drawItem.finalBones.begin(), drawItem.finalBones.end());
    }

    struct MeshDrawReference
    {
        Entity *entity{nullptr};
        DrawItem *drawItem{nullptr};
        uint32_t meshIndex{0};
        GPUMesh::SharedPtr mesh{nullptr};
        Material::SharedPtr material{nullptr};
        glm::vec3 worldBoundsCenter{0.0f};
        float worldBoundsRadius{0.0f};
        bool skinned{false};
        glm::mat4 modelMatrix{1.0f};
    };

    const uint64_t skinnedVertexLayoutHash = vertex::VertexTraits<vertex::VertexSkinned>::layout().hash;

    std::vector<MeshDrawReference> drawReferences;
    drawReferences.reserve(m_perFrameData.drawItems.size() * 2);

    for (auto &[entity, drawItem] : m_perFrameData.drawItems)
    {
        const bool hasSkeletonPalette = !drawItem.finalBones.empty();

        for (uint32_t meshIndex = 0; meshIndex < drawItem.meshes.size(); ++meshIndex)
        {
            const auto &mesh = drawItem.meshes[meshIndex];
            if (!mesh)
                continue;

            const bool meshIsSkinned = hasSkeletonPalette && mesh->vertexLayoutHash == skinnedVertexLayoutHash;

            const glm::mat4 localTransform = meshIndex < drawItem.localMeshTransforms.size()
                                                 ? drawItem.localMeshTransforms[meshIndex]
                                                 : glm::mat4(1.0f);
            const glm::mat4 modelMatrix = drawItem.transform * localTransform;

            auto material = mesh->material ? mesh->material : Material::getDefaultMaterial();
            mesh->material = material;

            glm::vec3 worldBoundsCenter{0.0f};
            float worldBoundsRadius{0.0f};
            if (meshIndex < drawItem.cachedWorldBoundsCenters.size())
            {
                worldBoundsCenter = drawItem.cachedWorldBoundsCenters[meshIndex];
                worldBoundsRadius = drawItem.cachedWorldBoundsRadii[meshIndex];
            }

            drawReferences.push_back(MeshDrawReference{
                .entity = entity,
                .drawItem = &drawItem,
                .meshIndex = meshIndex,
                .mesh = mesh,
                .material = material,
                .worldBoundsCenter = worldBoundsCenter,
                .worldBoundsRadius = worldBoundsRadius,
                .skinned = meshIsSkinned,
                .modelMatrix = modelMatrix});
        }
    }

    auto isTranslucentReference = [](const MeshDrawReference &reference) -> bool
    {
        if (!reference.material)
            return false;

        const uint32_t flags = reference.material->params().flags;
        return (flags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u;
    };

    std::sort(drawReferences.begin(), drawReferences.end(),
              [cameraPosition, &isTranslucentReference](const MeshDrawReference &left, const MeshDrawReference &right)
              {
                  const bool leftTranslucent = isTranslucentReference(left);
                  const bool rightTranslucent = isTranslucentReference(right);
                  if (leftTranslucent != rightTranslucent)
                      return !leftTranslucent;

                  if (leftTranslucent && rightTranslucent)
                  {
                      const glm::vec3 leftDelta = left.worldBoundsCenter - cameraPosition;
                      const glm::vec3 rightDelta = right.worldBoundsCenter - cameraPosition;
                      const float leftDistanceSquared = glm::dot(leftDelta, leftDelta);
                      const float rightDistanceSquared = glm::dot(rightDelta, rightDelta);
                      if (std::abs(leftDistanceSquared - rightDistanceSquared) > 1e-6f)
                          return leftDistanceSquared > rightDistanceSquared; // back-to-front for blending
                  }

                  if (left.skinned != right.skinned)
                      return left.skinned < right.skinned;

                  if (left.mesh->vertexBuffer.get() != right.mesh->vertexBuffer.get())
                      return left.mesh->vertexBuffer.get() < right.mesh->vertexBuffer.get();

                  if (left.mesh->indexBuffer.get() != right.mesh->indexBuffer.get())
                      return left.mesh->indexBuffer.get() < right.mesh->indexBuffer.get();

                  if (left.material.get() != right.material.get())
                      return left.material.get() < right.material.get();

                  if (left.meshIndex != right.meshIndex)
                      return left.meshIndex < right.meshIndex;

                  const uint32_t leftEntityId = left.entity ? left.entity->getId() : 0u;
                  const uint32_t rightEntityId = right.entity ? right.entity->getId() : 0u;
                  return leftEntityId < rightEntityId;
              });

    m_perFrameData.perObjectInstances.clear();
    m_perFrameData.perObjectInstances.reserve(drawReferences.size());
    m_perFrameData.drawBatches.clear();
    m_perFrameData.drawBatches.reserve(drawReferences.size());

    for (auto &batches : m_perFrameData.directionalShadowDrawBatches)
        batches.clear();
    for (auto &batches : m_perFrameData.spotShadowDrawBatches)
        batches.clear();
    for (auto &batches : m_perFrameData.pointShadowDrawBatches)
        batches.clear();

    auto hasSameGeometry = [](const GPUMesh::SharedPtr &left, const GPUMesh::SharedPtr &right) -> bool
    {
        if (!left || !right)
            return false;

        return left->vertexBuffer.get() == right->vertexBuffer.get() &&
               left->indexBuffer.get() == right->indexBuffer.get() &&
               left->indicesCount == right->indicesCount &&
               left->indexType == right->indexType &&
               left->vertexStride == right->vertexStride &&
               left->vertexLayoutHash == right->vertexLayoutHash;
    };

    for (const auto &reference : drawReferences)
    {
        const uint32_t instanceIndex = static_cast<uint32_t>(m_perFrameData.perObjectInstances.size());

        PerObjectInstanceData instanceData{};
        instanceData.model = reference.modelMatrix;
        instanceData.objectInfo = glm::uvec4(
            render::encodeObjectId(reference.entity->getId(), reference.meshIndex),
            reference.drawItem->bonesOffset,
            0u,
            0u);
        m_perFrameData.perObjectInstances.push_back(instanceData);

        if (!m_perFrameData.drawBatches.empty())
        {
            auto &lastBatch = m_perFrameData.drawBatches.back();
            if (lastBatch.skinned == reference.skinned &&
                lastBatch.material.get() == reference.material.get() &&
                hasSameGeometry(lastBatch.mesh, reference.mesh) &&
                lastBatch.firstInstance + lastBatch.instanceCount == instanceIndex)
            {
                ++lastBatch.instanceCount;
                continue;
            }
        }

        DrawBatch batch{};
        batch.mesh = reference.mesh;
        batch.material = reference.material;
        batch.skinned = reference.skinned;
        batch.firstInstance = instanceIndex;
        batch.instanceCount = 1;
        m_perFrameData.drawBatches.push_back(batch);
    }
    std::vector<const MeshDrawReference *> shadowReferences;
    shadowReferences.reserve(drawReferences.size());
    for (size_t referenceIndex = 0; referenceIndex < drawReferences.size(); ++referenceIndex)
    {
        const uint32_t materialFlags = drawReferences[referenceIndex].material ? drawReferences[referenceIndex].material->params().flags : 0u;
        const bool isTranslucent = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u;
        if (isTranslucent)
            continue;

        shadowReferences.push_back(&drawReferences[referenceIndex]);
    }

    std::sort(shadowReferences.begin(), shadowReferences.end(),
              [](const MeshDrawReference *left, const MeshDrawReference *right)
              {
                  if (left->skinned != right->skinned)
                      return left->skinned < right->skinned;

                  if (left->mesh->vertexBuffer.get() != right->mesh->vertexBuffer.get())
                      return left->mesh->vertexBuffer.get() < right->mesh->vertexBuffer.get();

                  if (left->mesh->indexBuffer.get() != right->mesh->indexBuffer.get())
                      return left->mesh->indexBuffer.get() < right->mesh->indexBuffer.get();

                  if (left->meshIndex != right->meshIndex)
                      return left->meshIndex < right->meshIndex;

                  const uint32_t leftEntityId = left->entity ? left->entity->getId() : 0u;
                  const uint32_t rightEntityId = right->entity ? right->entity->getId() : 0u;
                  return leftEntityId < rightEntityId;
              });

    std::vector<PerObjectInstanceData> shadowPerObjectInstances;
    {
        const uint32_t activeShadowExecutions = m_perFrameData.activeDirectionalCascadeCount +
                                                m_perFrameData.activeSpotShadowCount +
                                                m_perFrameData.activePointShadowCount * ShadowConstants::POINT_SHADOW_FACES;
        shadowPerObjectInstances.reserve(shadowReferences.size() * std::max<uint32_t>(activeShadowExecutions, 1u));
    }

    auto hasSameShadowKey = [&](const DrawBatch &batch, const MeshDrawReference &reference) -> bool
    {
        return batch.skinned == reference.skinned && hasSameGeometry(batch.mesh, reference.mesh);
    };

    auto buildShadowBatches = [&](std::vector<DrawBatch> &outBatches)
    {
        outBatches.clear();

        for (const MeshDrawReference *reference : shadowReferences)
        {
            if (!reference || !reference->mesh)
                continue;

            const uint32_t instanceIndex = static_cast<uint32_t>(shadowPerObjectInstances.size());
            PerObjectInstanceData instanceData{};
            instanceData.model = reference->modelMatrix;
            instanceData.objectInfo = glm::uvec4(
                render::encodeObjectId(reference->entity->getId(), reference->meshIndex),
                reference->drawItem->bonesOffset,
                0u,
                0u);
            shadowPerObjectInstances.push_back(instanceData);

            if (!outBatches.empty())
            {
                auto &lastBatch = outBatches.back();
                if (hasSameShadowKey(lastBatch, *reference) &&
                    lastBatch.firstInstance + lastBatch.instanceCount == instanceIndex)
                {
                    ++lastBatch.instanceCount;
                    continue;
                }
            }

            DrawBatch batch{};
            batch.mesh = reference->mesh;
            batch.material = reference->material;
            batch.skinned = reference->skinned;
            batch.firstInstance = instanceIndex;
            batch.instanceCount = 1;
            outBatches.push_back(batch);
        }
    };

    for (uint32_t cascadeIndex = 0; cascadeIndex < m_perFrameData.activeDirectionalCascadeCount; ++cascadeIndex)
        buildShadowBatches(m_perFrameData.directionalShadowDrawBatches[cascadeIndex]);

    for (uint32_t spotIndex = 0; spotIndex < m_perFrameData.activeSpotShadowCount; ++spotIndex)
        buildShadowBatches(m_perFrameData.spotShadowDrawBatches[spotIndex]);

    for (uint32_t pointIndex = 0; pointIndex < m_perFrameData.activePointShadowCount; ++pointIndex)
    {
        for (uint32_t faceIndex = 0; faceIndex < ShadowConstants::POINT_SHADOW_FACES; ++faceIndex)
        {
            const uint32_t matrixIndex = pointIndex * ShadowConstants::POINT_SHADOW_FACES + faceIndex;
            buildShadowBatches(m_perFrameData.pointShadowDrawBatches[matrixIndex]);
        }
    }

    const VkDeviceSize requiredBonesSize = static_cast<VkDeviceSize>(frameBones.size() * sizeof(glm::mat4));
    const VkDeviceSize availableBonesSize = m_bonesSSBOs[m_currentFrame]->getSize();
    if (requiredBonesSize > availableBonesSize)
        throw std::runtime_error("Bones SSBO size is too small for current frame. Increase initial bones buffer size.");

    const VkDeviceSize requiredInstanceSize = static_cast<VkDeviceSize>(m_perFrameData.perObjectInstances.size() * sizeof(PerObjectInstanceData));
    const VkDeviceSize availableInstanceSize = m_instanceSSBOs[m_currentFrame]->getSize();
    if (requiredInstanceSize > availableInstanceSize)
        throw std::runtime_error("Instance SSBO size is too small for current frame. Increase initial instance buffer size.");

    const VkDeviceSize requiredShadowInstanceSize = static_cast<VkDeviceSize>(shadowPerObjectInstances.size() * sizeof(PerObjectInstanceData));
    const VkDeviceSize availableShadowInstanceSize = m_shadowInstanceSSBOs[m_currentFrame]->getSize();
    if (requiredShadowInstanceSize > availableShadowInstanceSize)
        throw std::runtime_error("Shadow instance SSBO size is too small for current frame. Increase initial shadow instance buffer size.");

    glm::mat4 *bonesMapped = nullptr;
    m_bonesSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(bonesMapped));
    if (!frameBones.empty())
        std::memcpy(bonesMapped, frameBones.data(), frameBones.size() * sizeof(glm::mat4));
    m_bonesSSBOs[m_currentFrame]->unmap();

    PerObjectInstanceData *instancesMapped = nullptr;
    m_instanceSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(instancesMapped));
    if (!m_perFrameData.perObjectInstances.empty())
        std::memcpy(instancesMapped, m_perFrameData.perObjectInstances.data(), requiredInstanceSize);
    m_instanceSSBOs[m_currentFrame]->unmap();

    PerObjectInstanceData *shadowInstancesMapped = nullptr;
    m_shadowInstanceSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(shadowInstancesMapped));
    if (!shadowPerObjectInstances.empty())
        std::memcpy(shadowInstancesMapped, shadowPerObjectInstances.data(), requiredShadowInstanceSize);
    m_shadowInstanceSSBOs[m_currentFrame]->unmap();

    m_perFrameData.perObjectDescriptorSet = m_perObjectDescriptorSets[m_currentFrame];
    m_perFrameData.shadowPerObjectDescriptorSet = m_shadowPerObjectDescriptorSets[m_currentFrame];
}

void RenderGraph::prepareFrame(Camera::SharedPtr camera, Scene *scene, float deltaTime)
{
    m_perFrameData.swapChainViewport = m_swapchain->getViewport();
    m_perFrameData.swapChainScissor = m_swapchain->getScissor();
    m_perFrameData.cameraDescriptorSet = m_cameraDescriptorSets[m_currentFrame];
    m_perFrameData.previewCameraDescriptorSet = m_previewCameraDescriptorSets[m_currentFrame];
    m_perFrameData.deltaTime = deltaTime;

    CameraUBO cameraUBO{};

    cameraUBO.view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
    cameraUBO.projection = camera ? camera->getProjectionMatrix() : glm::mat4(1.0f);
    cameraUBO.projection[1][1] *= -1;
    cameraUBO.invProjection = glm::inverse(cameraUBO.projection);
    cameraUBO.invView = glm::inverse(cameraUBO.view);

    m_perFrameData.projection = cameraUBO.projection;
    m_perFrameData.view = cameraUBO.view;

    std::memcpy(m_cameraMapped[m_currentFrame], &cameraUBO, sizeof(CameraUBO));

    CameraUBO previewCameraUBO{};
    previewCameraUBO.view = glm::lookAt(
        glm::vec3(0, 0, 3),
        glm::vec3(0, 0, 0),
        glm::vec3(0, 1, 0));

    previewCameraUBO.projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 10.0f);
    previewCameraUBO.projection[1][1] *= -1;
    previewCameraUBO.invProjection = glm::inverse(previewCameraUBO.projection);
    previewCameraUBO.invView = glm::inverse(previewCameraUBO.view);

    m_perFrameData.previewProjection = previewCameraUBO.projection;
    m_perFrameData.previewView = previewCameraUBO.view;
    m_perFrameData.skyboxHDRPath = scene ? scene->getSkyboxHDRPath() : std::string{};

    std::memcpy(m_previewCameraMapped[m_currentFrame], &previewCameraUBO, sizeof(CameraUBO));

    const auto lights = scene->getLights();

    void *mapped = nullptr;
    m_lightSSBOs[m_currentFrame]->map(mapped);

    LightSSBO *ssboData = static_cast<LightSSBO *>(mapped);
    ssboData->lightCount = static_cast<int>(lights.size());

    const glm::mat4 view = cameraUBO.view;
    const glm::mat3 view3 = glm::mat3(view);

    m_perFrameData.lightSpaceMatrix = glm::mat4(1.0f);
    m_perFrameData.directionalLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_perFrameData.directionalCascadeSplits.fill(std::numeric_limits<float>::max());
    m_perFrameData.spotLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_perFrameData.pointLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_perFrameData.activeDirectionalCascadeCount = 0;
    m_perFrameData.activeSpotShadowCount = 0;
    m_perFrameData.activePointShadowCount = 0;
    m_perFrameData.directionalLightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    m_perFrameData.directionalLightStrength = 0.0f;
    m_perFrameData.hasDirectionalLight = false;
    m_perFrameData.skyLightEnabled = false;

    LightSpaceMatrixUBO lightSpaceMatrixUBO{};
    // lightSpaceMatrixUBO.lightSpaceMatrix = glm::mat4(1.0f);
    // for (auto &matrix : lightSpaceMatrixUBO.directionalLightSpaceMatrices)
    //     matrix = glm::mat4(1.0f);
    // lightSpaceMatrixUBO.directionalCascadeSplits = glm::vec4(std::numeric_limits<float>::max());
    // for (auto &matrix : lightSpaceMatrixUBO.spotLightSpaceMatrices)
    //     matrix = glm::mat4(1.0f);

    const std::array<glm::vec3, ShadowConstants::POINT_SHADOW_FACES> pointFaceDirections{
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)};

    const std::array<glm::vec3, ShadowConstants::POINT_SHADOW_FACES> pointFaceUps{
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f)};

    bool directionalShadowAssigned = false;
    bool directionalDataAssigned = false;

    for (size_t i = 0; i < lights.size(); ++i)
    {
        const auto &lightComponent = lights[i];

        ssboData->lights[i].position = glm::vec4(0.0f);
        ssboData->lights[i].direction = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        ssboData->lights[i].parameters = glm::vec4(1.0f);
        ssboData->lights[i].colorStrength = glm::vec4(lightComponent->color, lightComponent->strength);
        ssboData->lights[i].shadowInfo = glm::vec4(0.0f);

        if (auto directionalLight = dynamic_cast<DirectionalLight *>(lightComponent.get()))
        {
            m_perFrameData.hasDirectionalLight = true;

            glm::vec3 dirWorld = glm::normalize(directionalLight->direction);
            glm::vec3 dirView = glm::normalize(view3 * dirWorld);

            ssboData->lights[i].direction = glm::vec4(dirView, 0.0f);
            ssboData->lights[i].parameters.w = 0.0f;

            if (!directionalDataAssigned)
            {
                m_perFrameData.directionalLightDirection = dirWorld;
                m_perFrameData.skyLightEnabled = directionalLight->skyLightEnabled;
                m_perFrameData.directionalLightStrength = directionalLight->skyLightEnabled ? directionalLight->strength : 0.0f;
                directionalDataAssigned = true;
            }

            if (directionalLight->castsShadows && !directionalShadowAssigned)
            {
                const float cameraNear = camera ? std::max(camera->getNear(), 0.01f) : 0.1f;
                const float sceneCameraFar = camera ? std::max(camera->getFar(), cameraNear + 0.1f) : 1000.0f;
                const float shadowMaxDistance = std::max(RenderQualitySettings::getInstance().shadowMaxDistance, cameraNear + 1.0f);
                const float cameraFar = std::min(sceneCameraFar, shadowMaxDistance);
                const float cameraFov = camera ? camera->getFOV() : 60.0f;
                const float cameraAspect = camera ? std::max(camera->getAspect(), 0.001f)
                                                  : static_cast<float>(m_swapchain->getExtent().width) / std::max(1.0f, static_cast<float>(m_swapchain->getExtent().height));

                glm::mat4 invView = glm::inverse(cameraUBO.view);
                glm::vec3 camPos = glm::vec3(invView[3]);
                glm::vec3 camForward = glm::normalize(glm::vec3(invView * glm::vec4(0, 0, -1, 0)));
                glm::vec3 camUp = glm::normalize(glm::vec3(invView * glm::vec4(0, 1, 0, 0)));
                glm::vec3 camRight = glm::normalize(glm::cross(camForward, camUp));
                if (glm::length(camRight) < 0.001f)
                    camRight = glm::vec3(1.0f, 0.0f, 0.0f);

                const uint32_t configuredCascadeCount = std::max(1u, std::min(RenderQualitySettings::getInstance().getShadowCascadeCount(), ShadowConstants::MAX_DIRECTIONAL_CASCADES));
                const float cascadeLambda = 0.85f;
                std::array<float, ShadowConstants::MAX_DIRECTIONAL_CASCADES + 1> cascadeDepths{};
                cascadeDepths[0] = cameraNear;

                for (uint32_t cascadeIndex = 0; cascadeIndex < configuredCascadeCount; ++cascadeIndex)
                {
                    const float p = static_cast<float>(cascadeIndex + 1) / static_cast<float>(configuredCascadeCount);
                    const float logSplit = cameraNear * std::pow(cameraFar / cameraNear, p);
                    const float uniformSplit = cameraNear + (cameraFar - cameraNear) * p;
                    cascadeDepths[cascadeIndex + 1] = glm::mix(uniformSplit, logSplit, cascadeLambda);
                }

                for (uint32_t cascadeIndex = 0; cascadeIndex < configuredCascadeCount; ++cascadeIndex)
                {
                    const float splitNear = cascadeDepths[cascadeIndex];
                    const float splitFar = cascadeDepths[cascadeIndex + 1];

                    const float tanHalfFov = std::tan(glm::radians(cameraFov * 0.5f));
                    const float nearHeight = splitNear * tanHalfFov;
                    const float nearWidth = nearHeight * cameraAspect;
                    const float farHeight = splitFar * tanHalfFov;
                    const float farWidth = farHeight * cameraAspect;

                    const glm::vec3 nearCenter = camPos + camForward * splitNear;
                    const glm::vec3 farCenter = camPos + camForward * splitFar;

                    std::array<glm::vec3, 8> corners{
                        nearCenter + camUp * nearHeight - camRight * nearWidth,
                        nearCenter + camUp * nearHeight + camRight * nearWidth,
                        nearCenter - camUp * nearHeight - camRight * nearWidth,
                        nearCenter - camUp * nearHeight + camRight * nearWidth,
                        farCenter + camUp * farHeight - camRight * farWidth,
                        farCenter + camUp * farHeight + camRight * farWidth,
                        farCenter - camUp * farHeight - camRight * farWidth,
                        farCenter - camUp * farHeight + camRight * farWidth};

                    glm::vec3 cascadeCenter{0.0f};
                    for (const auto &corner : corners)
                        cascadeCenter += corner;
                    cascadeCenter /= static_cast<float>(corners.size());

                    glm::vec3 lightUp = (std::abs(glm::dot(dirWorld, glm::vec3(0, 1, 0))) > 0.95f)
                                            ? glm::vec3(0, 0, 1)
                                            : glm::vec3(0, 1, 0);
                    const float lightDistance = splitFar + 50.0f;
                    glm::mat4 lightView = glm::lookAt(cascadeCenter - dirWorld * lightDistance, cascadeCenter, lightUp);

                    glm::vec3 minBounds(std::numeric_limits<float>::max());
                    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

                    for (const auto &corner : corners)
                    {
                        glm::vec3 cornerLight = glm::vec3(lightView * glm::vec4(corner, 1.0f));
                        minBounds = glm::min(minBounds, cornerLight);
                        maxBounds = glm::max(maxBounds, cornerLight);
                    }

                    constexpr float zPadding = 25.0f;
                    const float cascadeNear = std::max(0.1f, -maxBounds.z - zPadding);
                    const float cascadeFar = std::max(cascadeNear + 0.1f, -minBounds.z + zPadding);
                    glm::mat4 lightProj = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, cascadeNear, cascadeFar);
                    glm::mat4 lightMatrix = lightProj * lightView;

                    m_perFrameData.directionalLightSpaceMatrices[cascadeIndex] = lightMatrix;
                    m_perFrameData.directionalCascadeSplits[cascadeIndex] = splitFar;
                    lightSpaceMatrixUBO.directionalLightSpaceMatrices[cascadeIndex] = lightMatrix;
                    lightSpaceMatrixUBO.directionalCascadeSplits[cascadeIndex] = splitFar;

                    if (cascadeIndex == 0)
                    {
                        m_perFrameData.lightSpaceMatrix = lightMatrix;
                        lightSpaceMatrixUBO.lightSpaceMatrix = lightMatrix;
                    }
                }

                m_perFrameData.activeDirectionalCascadeCount = configuredCascadeCount;
                ssboData->lights[i].shadowInfo.x = 1.0f;
                directionalShadowAssigned = true;
            }
        }
        else if (auto pointLight = dynamic_cast<PointLight *>(lightComponent.get()))
        {
            glm::vec3 posWorld = lightComponent->position;
            glm::vec3 posView = glm::vec3(view * glm::vec4(posWorld, 1.0f));

            ssboData->lights[i].position = glm::vec4(posView, 1.0f);
            ssboData->lights[i].parameters.z = pointLight->radius;
            ssboData->lights[i].parameters.w = 2.0f;

            if (pointLight->castsShadows && m_perFrameData.activePointShadowCount < ShadowConstants::MAX_POINT_SHADOWS)
            {
                const uint32_t pointShadowIndex = m_perFrameData.activePointShadowCount++;
                const float nearPlane = 0.1f;
                const float farPlane = std::max(pointLight->radius, nearPlane + 0.1f);
                const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

                for (uint32_t face = 0; face < ShadowConstants::POINT_SHADOW_FACES; ++face)
                {
                    const glm::mat4 faceView = glm::lookAt(posWorld, posWorld + pointFaceDirections[face], pointFaceUps[face]);
                    const uint32_t matrixIndex = pointShadowIndex * ShadowConstants::POINT_SHADOW_FACES + face;
                    m_perFrameData.pointLightSpaceMatrices[matrixIndex] = projection * faceView;
                }

                ssboData->lights[i].shadowInfo = glm::vec4(1.0f, static_cast<float>(pointShadowIndex), farPlane, nearPlane);
            }
        }
        else if (auto spotLight = dynamic_cast<SpotLight *>(lightComponent.get()))
        {
            glm::vec3 posView = glm::vec3(view * glm::vec4(lightComponent->position, 1.0f));
            glm::vec3 dirView = glm::normalize(view3 * glm::normalize(spotLight->direction));

            ssboData->lights[i].position = glm::vec4(posView, 1.0f);
            ssboData->lights[i].direction = glm::vec4(dirView, 0.0f);

            ssboData->lights[i].parameters.w = 1.0f;
            ssboData->lights[i].parameters.x = glm::cos(glm::radians(spotLight->innerAngle));
            ssboData->lights[i].parameters.y = glm::cos(glm::radians(spotLight->outerAngle));
            ssboData->lights[i].parameters.z = std::max(spotLight->range, 0.1f);

            if (spotLight->castsShadows && m_perFrameData.activeSpotShadowCount < ShadowConstants::MAX_SPOT_SHADOWS)
            {
                const uint32_t spotShadowIndex = m_perFrameData.activeSpotShadowCount++;
                const glm::vec3 positionWorld = lightComponent->position;
                const glm::vec3 directionWorld = glm::normalize(spotLight->direction);
                const glm::vec3 up = (std::abs(glm::dot(directionWorld, glm::vec3(0, 1, 0))) > 0.95f)
                                         ? glm::vec3(0, 0, 1)
                                         : glm::vec3(0, 1, 0);

                const float nearPlane = 0.1f;
                const float farPlane = std::max(spotLight->range, nearPlane + 0.1f);
                const float fullConeAngle = std::max(spotLight->outerAngle * 2.0f, 1.0f);
                const glm::mat4 lightView = glm::lookAt(positionWorld, positionWorld + directionWorld, up);
                const glm::mat4 lightProjection = glm::perspective(glm::radians(fullConeAngle), 1.0f, nearPlane, farPlane);
                const glm::mat4 lightMatrix = lightProjection * lightView;

                m_perFrameData.spotLightSpaceMatrices[spotShadowIndex] = lightMatrix;
                lightSpaceMatrixUBO.spotLightSpaceMatrices[spotShadowIndex] = lightMatrix;
                ssboData->lights[i].shadowInfo = glm::vec4(1.0f, static_cast<float>(spotShadowIndex), farPlane, 0.0f);
            }
        }
    }

    m_lightSSBOs[m_currentFrame]->unmap();
    std::memcpy(m_lightMapped[m_currentFrame], &lightSpaceMatrixUBO, sizeof(LightSpaceMatrixUBO));

    prepareFrameDataFromScene(scene, cameraUBO.view, cameraUBO.projection, camera != nullptr);
}

void RenderGraph::createDescriptorSetPool()
{
    const std::vector<VkDescriptorPoolSize> poolSizes{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100}};

    // Camera + preview camera + per-object + shadow-per-object descriptor sets per frame.
    static constexpr uint32_t kRenderGraphSetGroupsPerFrame = 4;
    const uint32_t maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * kRenderGraphSetGroupsPerFrame;

    m_descriptorPool = core::DescriptorPool::createShared(m_device, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                                                          maxSets);
}

void RenderGraph::recreateSwapChain()
{
    m_swapchain->recreate();

    auto barriers = m_renderGraphPassesCompiler.onSwapChainResized(m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

    auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer.begin();

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep.pImageMemoryBarriers = barriers.data();

    vkCmdPipelineBarrier2(commandBuffer, &dep);

    commandBuffer.end();

    commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
    {
        std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    }

    for (const auto &[id, renderPass] : m_renderGraphPasses)
        renderPass.renderGraphPass->compile(m_renderGraphPassesStorage);

    invalidateAllExecutionCaches();
}

bool RenderGraph::begin()
{
    utilities::AsyncGpuUpload::collectFinished(m_device);
    m_renderGraphProfiling->syncDetailedProfilingMode();

    // NOTE: do NOT reset the slot yet — it still holds complete profiling data from the
    // previous time this frame slot was used (2 frames ago). We resolve it below before
    // overwriting it, so that all fields (commandPoolResetMs, primaryCbEndMs, submitMs,
    // presentMs, etc.) are read correctly instead of as zeros.
    const bool detailedProfilingEnabled = m_renderGraphProfiling->isDetailedProfilingEnabled();

    const VkResult waitForFenceResult = m_renderGraphProfiling->measureCpuStage(
        m_currentFrame,
        RenderGraphProfiling::CpuStage::WaitForFence,
        [&]()
        {
            return vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
        });
    const double currentFenceWaitMs = m_renderGraphProfiling->getCpuStageProfilingDataByFrame(m_currentFrame).waitForFenceMs;

    if (waitForFenceResult != VK_SUCCESS)
    {
        m_renderGraphProfiling->onFenceWaitFailure(m_currentFrame, currentFenceWaitMs);
        VX_ENGINE_ERROR_STREAM("Failed to wait for fences: " << core::helpers::vulkanResultToString(waitForFenceResult) << '\n');
        return false;
    }

    // Resize/fullscreen events can arrive while UI code is building a frame.
    // Recreate swapchain only from here, after fence wait, to avoid re-entrant resize work.
    if (m_presentToSwapchain && m_swapchainResizeRequested.exchange(false, std::memory_order_relaxed))
        recreateSwapChain();

    if (!m_uploadWaitSemaphoresByFrame[m_currentFrame].empty())
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, m_uploadWaitSemaphoresByFrame[m_currentFrame]);
        m_uploadWaitSemaphoresByFrame[m_currentFrame].clear();
    }

    if (m_presentToSwapchain && m_swapchain)
    {
        const bool desiredVSyncState = RenderQualitySettings::getInstance().enableVSync;
        if (m_swapchain->isVSyncEnabled() != desiredVSyncState)
        {
            m_swapchain->setVSyncEnabled(desiredVSyncState);
            recreateSwapChain();
            return false;
        }
    }

    if (VkResult result = vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]); result != VK_SUCCESS)
    {
        VX_ENGINE_ERROR_STREAM("Failed to reset fences: " << core::helpers::vulkanResultToString(result) << '\n');
        return false;
    }

    // Resolve profiling for the previous use of this frame slot, then reset and seed
    // current CPU stage timings (fence wait + resolve profiling).
    m_renderGraphProfiling->beginFrameCpuProfiling(m_currentFrame, currentFenceWaitMs);

    m_renderGraphProfiling->measureCpuStage(
        m_currentFrame,
        RenderGraphProfiling::CpuStage::CommandPoolReset,
        [&]()
        {
            m_commandPools[m_currentFrame]->reset(0);
            for (const auto &secondaryPool : m_secondaryCommandPools[m_currentFrame])
            {
                if (secondaryPool)
                    secondaryPool->reset(0);
            }
        });

    if (m_presentToSwapchain)
    {
        VkResult result = m_renderGraphProfiling->measureCpuStage(
            m_currentFrame,
            RenderGraphProfiling::CpuStage::AcquireImage,
            [&]()
            {
                return vkAcquireNextImageKHR(m_device, m_swapchain->vk(), UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &m_imageIndex);
            });

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain();

            return false;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            VX_ENGINE_ERROR_STREAM("Failed to acquire swap chain image: " << core::helpers::vulkanResultToString(result) << '\n');
            return false;
        }
    }
    else
    {
        m_renderGraphProfiling->recordCpuStage(m_currentFrame, RenderGraphProfiling::CpuStage::AcquireImage, 0.0);
        const uint32_t imageCount = std::max(1u, m_swapchain->getImageCount());
        m_imageIndex = m_currentFrame % imageCount;
    }

    std::vector<uint32_t> dirtyPassIds;
    dirtyPassIds.reserve(m_renderGraphPasses.size());

    for (const auto &[id, renderGraphPass] : m_renderGraphPasses)
    {
        if (renderGraphPass.renderGraphPass->needsRecompilation())
            dirtyPassIds.push_back(renderGraphPass.id);
    }

    if (!dirtyPassIds.empty())
    {
        m_renderGraphProfiling->measureCpuStage(
            m_currentFrame,
            RenderGraphProfiling::CpuStage::Recompile,
            [&]()
            {
                vkDeviceWaitIdle(m_device);

                std::unordered_set<uint32_t> dirtyPassIdsSet(dirtyPassIds.begin(), dirtyPassIds.end());
                std::unordered_set<uint32_t> passesToCompile;
                std::queue<uint32_t> pendingPassIds;

                for (const uint32_t dirtyPassId : dirtyPassIds)
                    pendingPassIds.push(dirtyPassId);

                while (!pendingPassIds.empty())
                {
                    const uint32_t passId = pendingPassIds.front();
                    pendingPassIds.pop();

                    if (!passesToCompile.insert(passId).second)
                        continue;

                    auto *passData = findRenderGraphPassById(passId);
                    if (!passData)
                    {
                        VX_ENGINE_ERROR_STREAM("Failed to find pass while resolving recompilation dependencies: " << passId << '\n');
                        continue;
                    }

                    for (const uint32_t dependentPassId : passData->outgoing)
                        pendingPassIds.push(dependentPassId);
                }

                std::vector<RGPResourceHandler> resourcesToRecreate;
                resourcesToRecreate.reserve(m_renderGraphPasses.size() * 2);
                std::unordered_set<RGPResourceHandler> uniqueResources;

                for (const uint32_t dirtyPassId : dirtyPassIdsSet)
                {
                    const auto *passData = findRenderGraphPassById(dirtyPassId);
                    if (!passData)
                        continue;

                    for (const auto &write : passData->passInfo.writes)
                    {
                        if (uniqueResources.insert(write.resourceId).second)
                            resourcesToRecreate.push_back(write.resourceId);
                    }
                }

                auto barriers = m_renderGraphPassesCompiler.compile(resourcesToRecreate, m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

                if (!barriers.empty())
                {
                    auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
                    commandBuffer.begin();

                    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                    dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
                    dep.pImageMemoryBarriers = barriers.data();

                    vkCmdPipelineBarrier2(commandBuffer, &dep);

                    commandBuffer.end();

                    commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
                    {
                        std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
                        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
                    }
                }

                auto findPassDataByPtr = [&](const IRenderGraphPass *passPtr) -> RenderGraphPassData *
                {
                    for (auto &[_, passData] : m_renderGraphPasses)
                    {
                        if (passData.renderGraphPass.get() == passPtr)
                            return &passData;
                    }

                    return nullptr;
                };

                for (const auto *sortedPass : m_sortedRenderGraphPasses)
                {
                    auto *passData = findPassDataByPtr(sortedPass);
                    if (!passData)
                    {
                        VX_ENGINE_ERROR_STREAM("Failed to find sorted pass while recompiling render graph\n");
                        continue;
                    }

                    if (!passesToCompile.contains(passData->id))
                        continue;

                    passData->renderGraphPass->compile(m_renderGraphPassesStorage);
                }

                if (m_sortedRenderGraphPasses.empty())
                {
                    for (const uint32_t passId : passesToCompile)
                    {
                        auto *passData = findRenderGraphPassById(passId);
                        if (!passData)
                            continue;

                        passData->renderGraphPass->compile(m_renderGraphPassesStorage);
                    }
                }

                for (const uint32_t dirtyPassId : dirtyPassIdsSet)
                {
                    auto *passData = findRenderGraphPassById(dirtyPassId);
                    if (passData)
                        passData->renderGraphPass->recompilationIsDone();
                }

                // Invalidate execution cache for any pass that was recompiled
                // (its resources/image handles may have changed).
                if (m_passExecutionCache.size() == m_sortedRenderGraphPasses.size())
                {
                    for (size_t i = 0; i < m_sortedRenderGraphPasses.size(); ++i)
                    {
                        auto *passData = findPassDataByPtr(m_sortedRenderGraphPasses[i]);
                        if (passData && passesToCompile.contains(passData->id))
                            m_passExecutionCache[i].valid = false;
                    }
                }
            });
    }

    m_passContextData.currentFrame = m_currentFrame;
    m_passContextData.currentImageIndex = m_imageIndex;
    m_passContextData.executionIndex = 0;
    m_passContextData.activeDirectionalShadowCount = m_perFrameData.activeDirectionalCascadeCount;
    m_passContextData.activeSpotShadowCount = m_perFrameData.activeSpotShadowCount;
    m_passContextData.activePointShadowCount = m_perFrameData.activePointShadowCount;

    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
        renderGraphPass->prepareRecord(m_perFrameData, m_passContextData);

    // Ensure the cache is sized to match the current sorted pass list.
    if (m_passExecutionCache.size() != m_sortedRenderGraphPasses.size())
        m_passExecutionCache.resize(m_sortedRenderGraphPasses.size());

    struct PendingRenderExecution
    {
        IRenderGraphPass *pass{nullptr};
        core::CommandBuffer::SharedPtr secondaryCommandBuffer{nullptr};
        const IRenderGraphPass::RenderPassExecution *execution{nullptr};
        const std::vector<VkImageMemoryBarrier2> *preBarriers{nullptr};
        const std::vector<VkImageMemoryBarrier2> *postBarriers{nullptr};
        uint32_t executionIndex{0u};
        std::string passName;
        RenderGraphProfiling::PassExecutionProfilingData profilingData{};
        std::exception_ptr recordException{nullptr};
    };

    std::vector<PendingRenderExecution> pendingExecutions;
    pendingExecutions.reserve(MAX_RENDER_JOBS);

    size_t passIndex = 0;
    size_t secIndex = 0;
    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
    {
        auto &cachedData = m_passExecutionCache[passIndex];
        const bool cacheHit = cachedData.valid &&
                              cachedData.imageIndex == m_passContextData.currentImageIndex &&
                              cachedData.directionalShadowCount == m_passContextData.activeDirectionalShadowCount &&
                              cachedData.spotShadowCount == m_passContextData.activeSpotShadowCount &&
                              cachedData.pointShadowCount == m_passContextData.activePointShadowCount;
        if (!cacheHit)
            buildExecutionCacheForPass(passIndex);

        const auto &executions = cachedData.executions;
        for (size_t executionIndex = 0; executionIndex < executions.size(); ++executionIndex)
        {
            if (secIndex >= m_secondaryCommandBuffers[m_currentFrame].size())
                throw std::runtime_error("Not enough secondary command buffers preallocated for frame");

            PendingRenderExecution pending{};
            pending.pass = renderGraphPass;
            pending.secondaryCommandBuffer = m_secondaryCommandBuffers[m_currentFrame][secIndex];
            pending.execution = &executions[executionIndex];
            pending.preBarriers = &cachedData.preBarriers[executionIndex];
            pending.postBarriers = &cachedData.postBarriers[executionIndex];
            pending.executionIndex = static_cast<uint32_t>(executionIndex);
            pending.passName = renderGraphPass->getDebugName().empty() ? ("Pass " + std::to_string(passIndex))
                                                                       : renderGraphPass->getDebugName();
            pendingExecutions.push_back(std::move(pending));
            ++secIndex;
        }

        ++passIndex;
    }

    if (!pendingExecutions.empty())
    {
        const size_t recordingThreadCount = std::min<std::size_t>(pendingExecutions.size(), ThreadPoolManager::instance().getMaxThreads());
        ThreadPoolManager::instance().parallelFor(
            pendingExecutions.size(),
            [&](std::size_t beginIndex, std::size_t endIndex)
            {
                for (std::size_t pendingIndex = beginIndex; pendingIndex < endIndex; ++pendingIndex)
                {
                    auto &pending = pendingExecutions[pendingIndex];
                    const auto &execution = *pending.execution;

                    try
                    {
                        VkCommandBufferInheritanceRenderingInfo inheritanceRenderingInfo{
                            VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO};
                        inheritanceRenderingInfo.colorAttachmentCount = static_cast<uint32_t>(execution.colorsRenderingItems.size());
                        inheritanceRenderingInfo.pColorAttachmentFormats = execution.colorFormats.data();
                        inheritanceRenderingInfo.depthAttachmentFormat = execution.depthFormat;
                        inheritanceRenderingInfo.viewMask = 0;
                        inheritanceRenderingInfo.rasterizationSamples = execution.rasterizationSamples;
                        inheritanceRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

                        VkCommandBufferInheritanceInfo inheritanceInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
                        inheritanceInfo.pNext = &inheritanceRenderingInfo;
                        inheritanceInfo.subpass = 0;

                        if (!pending.secondaryCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, &inheritanceInfo))
                            throw std::runtime_error("Failed to begin secondary command buffer");

                        RenderGraphPassContext executionContext = m_passContextData;
                        executionContext.executionIndex = pending.executionIndex;

                        if (detailedProfilingEnabled)
                        {
                            profiling::ScopedDrawCallCounter scopedDrawCallCounter(pending.profilingData.drawCalls);
                            pending.pass->record(pending.secondaryCommandBuffer, m_perFrameData, executionContext);
                        }
                        else
                        {
                            pending.pass->record(pending.secondaryCommandBuffer, m_perFrameData, executionContext);
                        }

                        if (!pending.secondaryCommandBuffer->end())
                            throw std::runtime_error("Failed to end secondary command buffer");
                    }
                    catch (...)
                    {
                        pending.recordException = std::current_exception();
                    }
                }
            },
            recordingThreadCount);
    }

    const auto &primaryCommandBuffer = m_commandBuffers.at(m_currentFrame);

    primaryCommandBuffer->begin();
    m_renderGraphProfiling->beginFrameGpuProfiling(primaryCommandBuffer->vk(), m_currentFrame);

    for (auto &pending : pendingExecutions)
    {
        if (pending.recordException)
            std::rethrow_exception(pending.recordException);

        const auto &execution = *pending.execution;

        VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = execution.renderArea;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(execution.colorsRenderingItems.size());
        renderingInfo.pColorAttachments = execution.colorsRenderingItems.data();
        renderingInfo.pDepthAttachment = execution.useDepth ? &execution.depthRenderingItem : VK_NULL_HANDLE;
        renderingInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;

        m_renderGraphProfiling->beginPassProfiling(primaryCommandBuffer->vk(), m_currentFrame, pending.profilingData, pending.passName);

        std::chrono::high_resolution_clock::time_point cpuStartTime{};
        if (detailedProfilingEnabled)
            cpuStartTime = std::chrono::high_resolution_clock::now();

        VkDependencyInfo firstDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        firstDep.imageMemoryBarrierCount = static_cast<uint32_t>(pending.preBarriers->size());
        firstDep.pImageMemoryBarriers = pending.preBarriers->data();
        vkCmdPipelineBarrier2(primaryCommandBuffer, &firstDep);

        vkCmdBeginRendering(primaryCommandBuffer, &renderingInfo);
        vkCmdExecuteCommands(primaryCommandBuffer->vk(), 1, pending.secondaryCommandBuffer->pVk());
        vkCmdEndRendering(primaryCommandBuffer->vk());

        VkDependencyInfo secondDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        secondDep.imageMemoryBarrierCount = static_cast<uint32_t>(pending.postBarriers->size());
        secondDep.pImageMemoryBarriers = pending.postBarriers->data();
        vkCmdPipelineBarrier2(primaryCommandBuffer, &secondDep);

        if (detailedProfilingEnabled)
        {
            const auto cpuEndTime = std::chrono::high_resolution_clock::now();
            pending.profilingData.cpuTimeMs = std::chrono::duration<double, std::milli>(cpuEndTime - cpuStartTime).count();
        }

        m_renderGraphProfiling->endPassProfiling(primaryCommandBuffer->vk(), m_currentFrame, std::move(pending.profilingData));
    }

    m_renderGraphProfiling->endFrameGpuProfiling(primaryCommandBuffer->vk(), m_currentFrame);

    m_renderGraphProfiling->measureCpuStage(
        m_currentFrame,
        RenderGraphProfiling::CpuStage::PrimaryCommandBufferEnd,
        [&]()
        { primaryCommandBuffer->end(); });

    return true;
}

void RenderGraph::end()
{
    const auto &currentCommandBuffer = m_commandBuffers.at(m_currentFrame);
    std::vector<VkSemaphore> waitSemaphores;
    std::vector<VkPipelineStageFlags> waitStages;
    std::vector<VkSemaphore> signalSemaphores;
    const std::vector<VkSwapchainKHR> swapChains = {m_swapchain->vk()};

    if (m_presentToSwapchain)
    {
        waitSemaphores.push_back(m_imageAvailableSemaphores[m_currentFrame]);
        waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        signalSemaphores.push_back(m_renderFinishedSemaphores[m_currentFrame]);
    }

    utilities::AsyncGpuUpload::collectFinished(m_device);
    auto uploadWaitSemaphores = utilities::AsyncGpuUpload::acquireReadySemaphores();
    for (VkSemaphore uploadSemaphore : uploadWaitSemaphores)
    {
        waitSemaphores.push_back(uploadSemaphore);
        waitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }

    const bool submitOk = m_renderGraphProfiling->measureCpuStage(
        m_currentFrame,
        RenderGraphProfiling::CpuStage::Submit,
        [&]()
        {
            return currentCommandBuffer->submit(core::VulkanContext::getContext()->getGraphicsQueue(), waitSemaphores, waitStages, signalSemaphores,
                                                m_inFlightFences[m_currentFrame]);
        });

    if (!submitOk)
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, uploadWaitSemaphores);
        throw std::runtime_error("Failed to submit render graph command buffer");
    }

    m_uploadWaitSemaphoresByFrame[m_currentFrame] = std::move(uploadWaitSemaphores);

    m_renderGraphProfiling->markFrameSubmitted(m_currentFrame);

    if (m_presentToSwapchain)
    {
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        presentInfo.pWaitSemaphores = signalSemaphores.data();
        presentInfo.swapchainCount = static_cast<uint32_t>(swapChains.size());
        presentInfo.pSwapchains = swapChains.data();
        presentInfo.pImageIndices = &m_imageIndex;
        presentInfo.pResults = nullptr;

        VkResult result = m_renderGraphProfiling->measureCpuStage(
            m_currentFrame,
            RenderGraphProfiling::CpuStage::Present,
            [&]()
            {
                std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
                return vkQueuePresentKHR(core::VulkanContext::getContext()->getPresentQueue(), &presentInfo);
            });

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            recreateSwapChain();
        else if (result != VK_SUCCESS)
            throw std::runtime_error("Failed to present swap chain image: " + core::helpers::vulkanResultToString(result));
    }
    else
    {
        m_renderGraphProfiling->recordCpuStage(m_currentFrame, RenderGraphProfiling::CpuStage::Present, 0.0);
    }

    m_perFrameData.additionalData.clear();
}

void RenderGraph::draw()
{
    m_renderGraphProfiling->measureFrameCpuTime(
        m_currentFrame,
        [&]()
        {
            if (begin())
                end();
        });

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void RenderGraph::setup()
{
    std::vector<RenderGraphPassData *> setupOrder;
    setupOrder.reserve(m_renderGraphPasses.size());

    for (auto &[_, pass] : m_renderGraphPasses)
        setupOrder.push_back(&pass);

    std::sort(setupOrder.begin(), setupOrder.end(), [](const RenderGraphPassData *a, const RenderGraphPassData *b)
              { return a->id < b->id; });

    for (auto *passData : setupOrder)
    {
        if (!passData)
        {
            VX_ENGINE_ERROR_STREAM("failed to find render graph pass during setup\n");
            continue;
        }

        m_renderGraphPassesBuilder.setCurrentPass(&passData->passInfo);
        passData->renderGraphPass->setup(m_renderGraphPassesBuilder);
    }

    compile();
}

void RenderGraph::sortRenderGraphPasses()
{
    auto producerConsumer = [](const RenderGraphPassData &a, const RenderGraphPassData &b)
    {
        for (const auto &wa : a.passInfo.writes)
            for (const auto &rb : b.passInfo.reads)
                if (wa.resourceId == rb.resourceId)
                    return true;
        return false;
    };

    for (auto &[_, renderGraphPass] : m_renderGraphPasses)
    {
        renderGraphPass.outgoing.clear();
        renderGraphPass.indegree = 0;
    }

    auto addEdge = [&](uint32_t srcId, uint32_t dstId)
    {
        if (srcId == dstId)
            return;

        auto *srcPass = findRenderGraphPassById(srcId);
        auto *dstPass = findRenderGraphPassById(dstId);

        if (!srcPass || !dstPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to add graph edge " << srcId << " -> " << dstId << '\n');
            return;
        }

        if (std::find(srcPass->outgoing.begin(), srcPass->outgoing.end(), dstId) != srcPass->outgoing.end())
            return;

        srcPass->outgoing.push_back(dstId);
        dstPass->indegree++;
    };

    for (auto &[_, renderGraphPass] : m_renderGraphPasses)
    {
        for (auto &[_, secondRenderGraphPass] : m_renderGraphPasses)
        {
            if (renderGraphPass.id == secondRenderGraphPass.id)
                continue;

            if (producerConsumer(renderGraphPass, secondRenderGraphPass))
                addEdge(renderGraphPass.id, secondRenderGraphPass.id);
        }
    }

    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> q;
    std::vector<uint32_t> sorted;

    for (auto &[id, renderGraphPass] : m_renderGraphPasses)
        if (renderGraphPass.indegree <= 0)
            q.push(renderGraphPass.id);

    while (!q.empty())
    {
        uint32_t n = q.top();
        q.pop();
        sorted.push_back(n);

        auto renderGraphPass = findRenderGraphPassById(n);

        if (!renderGraphPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to find pass. Error...\n");
            continue;
        }

        for (uint32_t dst : renderGraphPass->outgoing)
        {
            auto dstRenderGraphPass = findRenderGraphPassById(dst);

            if (!dstRenderGraphPass)
            {
                VX_ENGINE_ERROR_STREAM("Failed to find dst pass. Error...\n");
                continue;
            }

            if (--dstRenderGraphPass->indegree == 0)
                q.push(dst);
        }
    }

    if (sorted.size() != m_renderGraphPasses.size())
    {
        VX_ENGINE_ERROR_STREAM("Failed to build graph tree\n");
        return;
    }

    m_sortedRenderGraphPasses.clear();
    m_sortedRenderGraphPasses.reserve(sorted.size());

    for (const auto &sortId : sorted)
    {
        auto renderGraphPass = findRenderGraphPassById(sortId);

        if (!renderGraphPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to find sorted node\n");
            continue;
        }

        m_sortedRenderGraphPasses.push_back(renderGraphPass->renderGraphPass.get());
    }

    // Sorted order changed — execution cache is keyed by sorted position, must be rebuilt.
    m_passExecutionCache.assign(m_sortedRenderGraphPasses.size(), CachedPassExecutionData{});

    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
        VX_ENGINE_INFO_STREAM("Node: " << renderGraphPass->getDebugName());
}

void RenderGraph::invalidateAllExecutionCaches()
{
    m_passExecutionCache.assign(m_sortedRenderGraphPasses.size(), CachedPassExecutionData{});
}

void RenderGraph::buildExecutionCacheForPass(size_t sortedPassIndex)
{
    if (sortedPassIndex >= m_sortedRenderGraphPasses.size())
        return;

    auto &cached = m_passExecutionCache[sortedPassIndex];
    auto *pass = m_sortedRenderGraphPasses[sortedPassIndex];

    cached.executions = pass->getRenderPassExecutions(m_passContextData);
    cached.imageIndex = m_passContextData.currentImageIndex;
    cached.directionalShadowCount = m_passContextData.activeDirectionalShadowCount;
    cached.spotShadowCount = m_passContextData.activeSpotShadowCount;
    cached.pointShadowCount = m_passContextData.activePointShadowCount;

    const size_t execCount = cached.executions.size();
    cached.preBarriers.resize(execCount);
    cached.postBarriers.resize(execCount);

    for (size_t execIdx = 0; execIdx < execCount; ++execIdx)
    {
        const auto &exec = cached.executions[execIdx];
        auto &pre = cached.preBarriers[execIdx];
        auto &post = cached.postBarriers[execIdx];
        pre.clear();
        post.clear();
        pre.reserve(exec.targets.size());
        post.reserve(exec.targets.size());

        for (const auto &[id, target] : exec.targets)
        {
            auto textureDescription = m_renderGraphPassesBuilder.getTextureDescription(id);
            if (!textureDescription || !target)
                continue;

            auto srcInfoInitial = utilities::ImageUtilities::getSrcLayoutInfo(textureDescription->getInitialLayout());
            auto srcInfoFinal = utilities::ImageUtilities::getSrcLayoutInfo(textureDescription->getFinalLayout());
            auto dstInfoInitial = utilities::ImageUtilities::getDstLayoutInfo(textureDescription->getInitialLayout());
            auto dstInfoFinal = utilities::ImageUtilities::getDstLayoutInfo(textureDescription->getFinalLayout());
            auto aspect = utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription->getFormat());

            pre.push_back(utilities::ImageUtilities::insertImageMemoryBarrier(
                *target->getImage(),
                srcInfoFinal.accessMask,
                dstInfoInitial.accessMask,
                textureDescription->getFinalLayout(),
                textureDescription->getInitialLayout(),
                srcInfoFinal.stageMask,
                dstInfoInitial.stageMask,
                {aspect, 0, 1, 0, textureDescription->getArrayLayers()}));

            post.push_back(utilities::ImageUtilities::insertImageMemoryBarrier(
                *target->getImage(),
                srcInfoInitial.accessMask,
                dstInfoFinal.accessMask,
                textureDescription->getInitialLayout(),
                textureDescription->getFinalLayout(),
                srcInfoInitial.stageMask,
                dstInfoFinal.stageMask,
                {aspect, 0, 1, 0, textureDescription->getArrayLayers()}));
        }
    }

    cached.valid = true;
}

void RenderGraph::compile()
{
    auto barriers = m_renderGraphPassesCompiler.compile(m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

    // VX_ENGINE_INFO_STREAM("Memory before render graphs compile: " << core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM() << '\n');

    auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer.begin();

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep.pImageMemoryBarriers = barriers.data();

    vkCmdPipelineBarrier2(commandBuffer, &dep);

    commandBuffer.end();

    commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
    {
        std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    }

    for (const auto &[id, pass] : m_renderGraphPasses)
        pass.renderGraphPass->compile(m_renderGraphPassesStorage);

    // Invalidate execution cache — resources may have been (re)allocated.
    // It will be lazily rebuilt on the first frame using the new VkImage handles.
    invalidateAllExecutionCaches();

    if (m_presentToSwapchain && !m_hasWindowResizeCallback)
    {
        m_hasWindowResizeCallback = true;
        core::VulkanContext::getContext()->getSwapchain()->getWindow().addResizeCallback([this](platform::Window *, int, int)
                                                                                         { m_swapchainResizeRequested.store(true, std::memory_order_relaxed); });
    }

    // VX_ENGINE_INFO_STREAM("Memory after render graphs compile: " << core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM() << '\n');
}

void RenderGraph::createPreviewCameraDescriptorSets()
{
    m_previewCameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_previewCameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_previewCameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &cameraBuffer = m_previewCameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_previewCameraMapped[i]);

        m_previewCameraDescriptorSets[i] = DescriptorSetBuilder::begin()
                                               .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                               .build(m_device, m_descriptorPool, EngineShaderFamilies::cameraDescriptorSetLayout->vk());
    }
}

void RenderGraph::createPerObjectDescriptorSets()
{
    m_perObjectDescriptorSets.resize(RenderGraph::MAX_FRAMES_IN_FLIGHT);
    m_shadowPerObjectDescriptorSets.resize(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    m_bonesSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);
    m_instanceSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);
    m_shadowInstanceSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    static constexpr VkDeviceSize bonesStructSize = sizeof(glm::mat4);
    static constexpr uint32_t INIT_BONES_COUNT = 1000u;
    static constexpr VkDeviceSize BONES_INITIAL_SIZE = bonesStructSize * INIT_BONES_COUNT;

    static constexpr VkDeviceSize instanceStructSize = sizeof(PerObjectInstanceData);
    static constexpr uint32_t INIT_INSTANCE_COUNT = 20000u;
    static constexpr VkDeviceSize INSTANCES_INITIAL_SIZE = instanceStructSize * INIT_INSTANCE_COUNT;
    static constexpr uint32_t INIT_SHADOW_INSTANCE_COUNT = 100000u;
    static constexpr VkDeviceSize SHADOW_INSTANCES_INITIAL_SIZE = instanceStructSize * INIT_SHADOW_INSTANCE_COUNT;

    for (size_t i = 0; i < RenderGraph::MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto bonesBuffer = m_bonesSSBOs.emplace_back(core::Buffer::createShared(BONES_INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                                core::memory::MemoryUsage::CPU_TO_GPU));
        auto instanceBuffer = m_instanceSSBOs.emplace_back(core::Buffer::createShared(INSTANCES_INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                                      core::memory::MemoryUsage::CPU_TO_GPU));
        auto shadowInstanceBuffer = m_shadowInstanceSSBOs.emplace_back(core::Buffer::createShared(SHADOW_INSTANCES_INITIAL_SIZE,
                                                                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                                                  core::memory::MemoryUsage::CPU_TO_GPU));

        m_perObjectDescriptorSets[i] = DescriptorSetBuilder::begin()
                                           .addBuffer(bonesBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                           .addBuffer(instanceBuffer, VK_WHOLE_SIZE, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                           .build(core::VulkanContext::getContext()->getDevice(), m_descriptorPool, EngineShaderFamilies::objectDescriptorSetLayout->vk());

        m_shadowPerObjectDescriptorSets[i] = DescriptorSetBuilder::begin()
                                                 .addBuffer(bonesBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                                 .addBuffer(shadowInstanceBuffer, VK_WHOLE_SIZE, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                                 .build(core::VulkanContext::getContext()->getDevice(), m_descriptorPool, EngineShaderFamilies::objectDescriptorSetLayout->vk());
    }
}

void RenderGraph::createCameraDescriptorSets()
{
    m_cameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightMapped.resize(MAX_FRAMES_IN_FLIGHT);

    m_lightSpaceMatrixUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_lightSSBOs.reserve(MAX_FRAMES_IN_FLIGHT);

    static constexpr uint8_t INIT_LIGHTS_COUNT = 2;
    static constexpr VkDeviceSize INITIAL_SIZE = sizeof(LightData) * (INIT_LIGHTS_COUNT * sizeof(LightData));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &cameraBuffer = m_cameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto &lightBuffer = m_lightSpaceMatrixUniformObjects.emplace_back(core::Buffer::createShared(sizeof(LightSpaceMatrixUBO),
                                                                                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto ssboBuffer = m_lightSSBOs.emplace_back(core::Buffer::createShared(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                               core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_cameraMapped[i]);
        lightBuffer->map(m_lightMapped[i]);

        m_cameraDescriptorSets[i] = DescriptorSetBuilder::begin()
                                        .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                        .addBuffer(lightBuffer, sizeof(LightSpaceMatrixUBO), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                        .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                        .build(m_device, m_descriptorPool, EngineShaderFamilies::cameraDescriptorSetLayout->vk());
    }

    createPreviewCameraDescriptorSets();
    createPerObjectDescriptorSets();
}

void RenderGraph::createRenderGraphResources()
{
    createDescriptorSetPool();
    createCameraDescriptorSets();

    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
    {
        auto commandPool = m_commandPools.emplace_back(core::CommandPool::createShared(core::VulkanContext::getContext()->getDevice(),
                                                                                       core::VulkanContext::getContext()->getGraphicsFamily()));

        m_commandBuffers.emplace_back(core::CommandBuffer::createShared(*commandPool));

        m_secondaryCommandPools[frame].resize(MAX_RENDER_JOBS);
        m_secondaryCommandBuffers[frame].resize(MAX_RENDER_JOBS);

        for (size_t job = 0; job < MAX_RENDER_JOBS; ++job)
        {
            auto secondaryPool = core::CommandPool::createShared(core::VulkanContext::getContext()->getDevice(),
                                                                 core::VulkanContext::getContext()->getGraphicsFamily());
            m_secondaryCommandPools[frame][job] = secondaryPool;
            m_secondaryCommandBuffers[frame][job] = core::CommandBuffer::createShared(*secondaryPool, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        }
    }

    m_renderGraphProfiling = std::make_unique<RenderGraphProfiling>(static_cast<uint32_t>(m_renderGraphPasses.size()));

    sortRenderGraphPasses();
    m_renderGraphProfiling->syncDetailedProfilingMode();
}

void RenderGraph::cleanResources()
{
    utilities::AsyncGpuUpload::flush(m_device);

    {
        std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    }
    vkDeviceWaitIdle(m_device);

    for (auto &uploadSemaphores : m_uploadWaitSemaphoresByFrame)
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, uploadSemaphores);
        uploadSemaphores.clear();
    }

    for (const auto &primary : m_commandBuffers)
        primary->destroyVk();
    for (const auto &secondary : m_secondaryCommandBuffers)
        for (const auto &cb : secondary)
            cb->destroyVk();

    for (const auto &renderPass : m_renderGraphPasses)
        renderPass.second.renderGraphPass->cleanup();

    for (auto &semaphore : m_imageAvailableSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }

    for (auto &semaphore : m_renderFinishedSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }

    for (auto &fence : m_inFlightFences)
    {
        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
    }

    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_swapchainResizeRequested.store(false, std::memory_order_relaxed);
    m_hasWindowResizeCallback = false;

    m_renderGraphProfiling->destroyTimestampQueryPool();
    m_renderGraphPassesStorage.cleanup();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
