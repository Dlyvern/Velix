#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/RagdollComponent.hpp"
#include "Engine/Entity.hpp"

#include "Engine/Assets/AssetsLoader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <system_error>

namespace
{
    std::string toLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return text;
    }

    bool looksLikeWindowsAbsolutePath(const std::string &path)
    {
        return path.size() >= 3u &&
               std::isalpha(static_cast<unsigned char>(path[0])) &&
               path[1] == ':' &&
               (path[2] == '\\' || path[2] == '/');
    }

    std::filesystem::path makeAbsoluteNormalized(const std::filesystem::path &path)
    {
        std::error_code errorCode;
        const std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
        if (errorCode)
            return path.lexically_normal();

        return absolutePath.lexically_normal();
    }

    std::optional<std::string> tryResolveExistingPath(const std::filesystem::path &candidatePath)
    {
        if (candidatePath.empty())
            return std::nullopt;

        const std::filesystem::path normalizedPath = makeAbsoluteNormalized(candidatePath);
        std::error_code existsError;
        if (std::filesystem::exists(normalizedPath, existsError) && !existsError)
            return normalizedPath.string();

        return std::nullopt;
    }

    std::string resolveProjectAssetPath(const std::string &assetPath)
    {
        if (assetPath.empty())
            return {};

        if (looksLikeWindowsAbsolutePath(assetPath))
            return assetPath;

        const std::filesystem::path parsedPath(assetPath);
        if (parsedPath.is_absolute())
            return makeAbsoluteNormalized(parsedPath).string();

        if (auto resolvedPath = tryResolveExistingPath(parsedPath); resolvedPath.has_value())
            return resolvedPath.value();

        const std::filesystem::path projectRoot = elix::engine::AssetsLoader::getTextureAssetImportRootDirectory();
        if (projectRoot.empty())
            return makeAbsoluteNormalized(parsedPath).string();

        if (auto resolvedPath = tryResolveExistingPath(projectRoot / parsedPath); resolvedPath.has_value())
            return resolvedPath.value();

        const std::string projectRootName = toLowerCopy(projectRoot.filename().string());
        auto segmentIterator = parsedPath.begin();
        if (!projectRootName.empty() &&
            segmentIterator != parsedPath.end() &&
            toLowerCopy(segmentIterator->string()) == projectRootName)
        {
            std::filesystem::path projectRelativePath;
            ++segmentIterator;
            for (; segmentIterator != parsedPath.end(); ++segmentIterator)
                projectRelativePath /= *segmentIterator;

            if (auto resolvedPath = tryResolveExistingPath(projectRoot / projectRelativePath); resolvedPath.has_value())
                return resolvedPath.value();
        }

        return makeAbsoluteNormalized(projectRoot / parsedPath).string();
    }

    std::string resolveTexturePathForMaterial(const std::string &texturePath,
                                              const std::filesystem::path &materialAssetPath)
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
            std::filesystem::path probeDirectory = makeAbsoluteNormalized(materialAssetPath.parent_path());
            while (!probeDirectory.empty())
            {
                if (auto resolvedPath = tryResolveExistingPath(probeDirectory / parsedPath); resolvedPath.has_value())
                    return resolvedPath.value();

                if (!probeDirectory.has_parent_path())
                    break;

                const std::filesystem::path parentDirectory = probeDirectory.parent_path();
                if (parentDirectory == probeDirectory)
                    break;

                probeDirectory = parentDirectory;
            }
        }

        const std::filesystem::path projectRoot = elix::engine::AssetsLoader::getTextureAssetImportRootDirectory();
        if (!projectRoot.empty())
        {
            if (auto resolvedPath = tryResolveExistingPath(projectRoot / parsedPath); resolvedPath.has_value())
                return resolvedPath.value();

            return makeAbsoluteNormalized(projectRoot / parsedPath).string();
        }

        return makeAbsoluteNormalized(parsedPath).string();
    }

    void normalizeMaterialTexturePaths(elix::engine::CPUMaterial &material,
                                       const std::filesystem::path &materialAssetPath)
    {
        material.albedoTexture = resolveTexturePathForMaterial(material.albedoTexture, materialAssetPath);
        material.normalTexture = resolveTexturePathForMaterial(material.normalTexture, materialAssetPath);
        material.ormTexture = resolveTexturePathForMaterial(material.ormTexture, materialAssetPath);
        material.emissiveTexture = resolveTexturePathForMaterial(material.emissiveTexture, materialAssetPath);
    }

    void applyMaterialOverrideCpuData(std::vector<elix::engine::CPUMesh> &meshes,
                                      const std::vector<std::string> &overridePaths)
    {
        const size_t overrideCount = std::min(meshes.size(), overridePaths.size());
        for (size_t slot = 0; slot < overrideCount; ++slot)
        {
            const std::string &overridePath = overridePaths[slot];
            if (overridePath.empty())
                continue;

            const std::string resolvedMaterialPath = resolveProjectAssetPath(overridePath);
            if (resolvedMaterialPath.empty())
                continue;

            auto materialAsset = elix::engine::AssetsLoader::loadMaterial(resolvedMaterialPath);
            if (!materialAsset.has_value())
                continue;

            auto material = materialAsset->material;
            normalizeMaterialTexturePaths(material, resolvedMaterialPath);
            meshes[slot].material = std::move(material);
        }
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

SkeletalMeshComponent::SkeletalMeshComponent(const std::vector<CPUMesh> &meshes, const Skeleton &skeleton)
    : m_meshes(meshes), m_skeleton(skeleton)
{
    m_skeleton.calculateBindPoseTransforms();
    m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
    m_perMeshMaterialOverridePaths.resize(m_meshes.size());
}

SkeletalMeshComponent::SkeletalMeshComponent(const std::string &assetPath)
    : m_assetPath(assetPath),
      m_modelHandle(assetPath)
{
}

const std::vector<CPUMesh> &SkeletalMeshComponent::getMeshes() const
{
    return m_meshes;
}

CPUMesh &SkeletalMeshComponent::getMesh(int index)
{
    return m_meshes[index];
}

void SkeletalMeshComponent::clearMaterialOverride(size_t slot)
{
    const size_t currentSize = std::max({m_meshes.size(), m_perMeshMaterialOverrides.size(), m_perMeshMaterialOverridePaths.size()});
    if (slot >= currentSize)
        return;

    const size_t requiredSize = std::max(currentSize, slot + 1);
    if (m_perMeshMaterialOverrides.size() < requiredSize)
        m_perMeshMaterialOverrides.resize(requiredSize, nullptr);
    if (m_perMeshMaterialOverridePaths.size() < requiredSize)
        m_perMeshMaterialOverridePaths.resize(requiredSize);

    m_perMeshMaterialOverrides[slot] = nullptr;
    m_perMeshMaterialOverridePaths[slot].clear();
}

void SkeletalMeshComponent::setMaterialOverridePath(size_t slot, const std::string &path)
{
    if (!m_meshes.empty() && slot >= m_meshes.size())
        return;

    const size_t requiredSize = std::max({m_meshes.size(), m_perMeshMaterialOverrides.size(), m_perMeshMaterialOverridePaths.size(), slot + 1});
    if (m_perMeshMaterialOverrides.size() < requiredSize)
        m_perMeshMaterialOverrides.resize(requiredSize, nullptr);
    if (m_perMeshMaterialOverridePaths.size() < requiredSize)
        m_perMeshMaterialOverridePaths.resize(requiredSize);

    m_perMeshMaterialOverridePaths[slot] = path;
}

const Skeleton &SkeletalMeshComponent::getSkeleton() const
{
    return m_skeleton;
}

Skeleton &SkeletalMeshComponent::getSkeleton()
{
    return m_skeleton;
}

bool SkeletalMeshComponent::isReady() const
{
    if (!m_meshes.empty())
        return true;
    return m_modelHandle.ready();
}

void SkeletalMeshComponent::applyMaterialOverrideCpuDataToMeshes()
{
    applyMaterialOverrideCpuData(m_meshes, m_perMeshMaterialOverridePaths);
}

void SkeletalMeshComponent::onModelLoaded()
{
    if (!m_modelHandle.ready())
        return;

    auto preservedMaterialOverrides = std::move(m_perMeshMaterialOverrides);
    auto preservedMaterialOverridePaths = std::move(m_perMeshMaterialOverridePaths);

    const auto &asset = m_modelHandle.get();
    m_meshes = asset.meshes;
    if (asset.skeleton.has_value())
    {
        m_skeleton = asset.skeleton.value();
        m_skeleton.calculateBindPoseTransforms();
    }

    m_perMeshMaterialOverrides.assign(m_meshes.size(), nullptr);
    m_perMeshMaterialOverridePaths.assign(m_meshes.size(), {});

    const size_t preservedOverrideCount = preservedMaterialOverrides.size() < m_meshes.size()
                                              ? preservedMaterialOverrides.size()
                                              : m_meshes.size();
    for (size_t slot = 0; slot < preservedOverrideCount; ++slot)
        m_perMeshMaterialOverrides[slot] = std::move(preservedMaterialOverrides[slot]);

    const size_t preservedPathCount = preservedMaterialOverridePaths.size() < m_meshes.size()
                                          ? preservedMaterialOverridePaths.size()
                                          : m_meshes.size();
    for (size_t slot = 0; slot < preservedPathCount; ++slot)
        m_perMeshMaterialOverridePaths[slot] = std::move(preservedMaterialOverridePaths[slot]);

    // Keep CPU mesh materials aligned with saved overrides so any fallback path
    // still resolves the authored material asset instead of stale importer paths.
    applyMaterialOverrideCpuDataToMeshes();

    // Skeleton loaded asynchronously — notify the AnimatorComponent on the same
    // entity so it can bind the skeleton to its tree clips.
    if (asset.skeleton.has_value())
    {
        if (auto *owner = getOwner<Entity>())
        {
            if (auto *animator = owner->getComponent<AnimatorComponent>())
                animator->bindSkeleton(&m_skeleton);
            if (auto *ragdoll = owner->getComponent<RagdollComponent>())
                ragdoll->buildFromProfile();
        }
    }
}

ELIX_NESTED_NAMESPACE_END
