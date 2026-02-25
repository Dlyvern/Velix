#ifndef ELIX_SKELETAL_MESH_COMPONENT_HPP
#define ELIX_SKELETAL_MESH_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Skeleton.hpp"
#include "Engine/Material.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SkeletalMeshComponent : public ECS
{
public:
    SkeletalMeshComponent(const std::vector<CPUMesh> &meshes, const Skeleton &skeleton);
    const std::vector<CPUMesh> &getMeshes() const;
    CPUMesh &getMesh(int index);

    void setMaterialOverride(size_t slot, Material::SharedPtr mat)
    {
        if (slot >= m_perMeshMaterialOverrides.size())
        {
            m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
            m_perMeshMaterialOverridePaths.resize(m_meshes.size());
        }

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
        if (slot < m_perMeshMaterialOverrides.size())
        {
            m_perMeshMaterialOverrides[slot] = nullptr;
            m_perMeshMaterialOverridePaths[slot].clear();
        }
    }

    void setMaterialOverridePath(size_t slot, const std::string &path)
    {
        if (slot >= m_perMeshMaterialOverridePaths.size())
        {
            m_perMeshMaterialOverrides.resize(m_meshes.size(), nullptr);
            m_perMeshMaterialOverridePaths.resize(m_meshes.size());
        }

        m_perMeshMaterialOverridePaths[slot] = path;
    }

    const std::string &getMaterialOverridePath(size_t slot) const
    {
        static const std::string emptyPath{};

        if (slot >= m_perMeshMaterialOverridePaths.size())
            return emptyPath;

        return m_perMeshMaterialOverridePaths[slot];
    }

    size_t getMaterialSlotCount() const { return m_meshes.size(); }

    const Skeleton &getSkeleton() const;
    Skeleton &getSkeleton();

private:
    std::vector<CPUMesh> m_meshes;
    Skeleton m_skeleton;
    std::vector<Material::SharedPtr> m_perMeshMaterialOverrides;
    std::vector<std::string> m_perMeshMaterialOverridePaths;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SKELETAL_MESH_COMPONENT_HPP
