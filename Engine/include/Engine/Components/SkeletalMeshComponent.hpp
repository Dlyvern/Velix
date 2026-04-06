#ifndef ELIX_SKELETAL_MESH_COMPONENT_HPP
#define ELIX_SKELETAL_MESH_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Assets/AssetHandle.hpp"
#include "Engine/Assets/Asset.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Skeleton.hpp"
#include "Engine/Material.hpp"

#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SkeletalMeshComponent : public ECS
{
public:
    // Legacy constructor.
    SkeletalMeshComponent(const std::vector<CPUMesh> &meshes, const Skeleton &skeleton);

    // Streaming constructor: store path only.
    explicit SkeletalMeshComponent(const std::string &assetPath);
    const std::vector<CPUMesh> &getMeshes() const;
    CPUMesh &getMesh(int index);

    void setMaterialOverride(size_t slot, Material::SharedPtr mat)
    {
        if (!m_meshes.empty() && slot >= m_meshes.size())
            return;

        const size_t requiredSize = std::max({m_meshes.size(), m_perMeshMaterialOverrides.size(), m_perMeshMaterialOverridePaths.size(), slot + 1});
        if (m_perMeshMaterialOverrides.size() < requiredSize)
            m_perMeshMaterialOverrides.resize(requiredSize, nullptr);
        if (m_perMeshMaterialOverridePaths.size() < requiredSize)
            m_perMeshMaterialOverridePaths.resize(requiredSize);

        m_perMeshMaterialOverrides[slot] = mat;
    }

    Material::SharedPtr getMaterialOverride(size_t slot) const
    {
        if (slot >= m_perMeshMaterialOverrides.size())
            return nullptr;
        return m_perMeshMaterialOverrides[slot];
    }

    void clearMaterialOverride(size_t slot)
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

    void setMaterialOverridePath(size_t slot, const std::string &path)
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

    const std::string &getMaterialOverridePath(size_t slot) const
    {
        static const std::string emptyPath{};

        if (slot >= m_perMeshMaterialOverridePaths.size())
            return emptyPath;

        return m_perMeshMaterialOverridePaths[slot];
    }

    size_t getMaterialSlotCount() const
    {
        if (!m_meshes.empty())
            return m_meshes.size();

        return std::max(m_perMeshMaterialOverrides.size(), m_perMeshMaterialOverridePaths.size());
    }

    const Skeleton &getSkeleton() const;
    Skeleton &getSkeleton();

    void setAssetPath(const std::string &path) { m_assetPath = path; }
    const std::string &getAssetPath() const { return m_assetPath; }

    // ---- On-demand streaming ----

    AssetHandle<ModelAsset>       &getModelHandle()       { return m_modelHandle; }
    const AssetHandle<ModelAsset> &getModelHandle() const { return m_modelHandle; }

    bool isReady() const;
    void onModelLoaded();

    // Called on unload — clears CPU mesh data so it can be GC'd.
    void clearMeshes() { m_meshes.clear(); m_perMeshMaterialOverrides.clear(); }

private:
    std::vector<CPUMesh> m_meshes;
    Skeleton m_skeleton;
    std::vector<Material::SharedPtr> m_perMeshMaterialOverrides;
    std::vector<std::string> m_perMeshMaterialOverridePaths;
    std::string m_assetPath;
    AssetHandle<ModelAsset> m_modelHandle;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SKELETAL_MESH_COMPONENT_HPP
