#ifndef ELIX_STATIC_MESH_COMPONENT_HPP
#define ELIX_STATIC_MESH_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Assets/AssetHandle.hpp"
#include "Engine/Assets/Asset.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Material.hpp"

#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class StaticMeshComponent : public ECS
{
public:
    // Legacy constructor: meshes already loaded (editor drag-drop, primitives, etc.)
    explicit StaticMeshComponent(const std::vector<CPUMesh> &meshes);

    // Streaming constructor: store path only, no disk I/O.
    // AssetManager::requestLoad() resolves the handle asynchronously.
    explicit StaticMeshComponent(const std::string &assetPath);

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

    void setAssetPath(const std::string &path) { m_assetPath = path; }
    const std::string &getAssetPath() const { return m_assetPath; }

    // ---- On-demand streaming ----

    // Returns the handle used for async loading.
    AssetHandle<ModelAsset>       &getModelHandle()       { return m_modelHandle; }
    const AssetHandle<ModelAsset> &getModelHandle() const { return m_modelHandle; }

    // True if either the handle is Ready OR meshes were set via legacy constructor.
    bool isReady() const;

    // Called by PerFrameDataWorker when handle transitions Ready → meshes populated.
    void onModelLoaded();

    // Called on unload — clears CPU mesh data so it can be GC'd.
    void clearMeshes() { m_meshes.clear(); m_perMeshMaterialOverrides.clear(); }

private:
    std::vector<CPUMesh> m_meshes;
    std::vector<Material::SharedPtr> m_perMeshMaterialOverrides;
    std::vector<std::string> m_perMeshMaterialOverridePaths;
    std::string m_assetPath;
    AssetHandle<ModelAsset> m_modelHandle;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_STATIC_MESH_COMPONENT_HPP
