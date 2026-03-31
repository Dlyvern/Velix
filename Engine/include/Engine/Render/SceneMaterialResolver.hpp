#ifndef ELIX_SCENE_MATERIAL_RESOLVER_HPP
#define ELIX_SCENE_MATERIAL_RESOLVER_HPP

#include "Core/Macros.hpp"
#include "Engine/Mesh.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class SceneMaterialResolver
{
public:
    void beginFrame(int maxNewMaterialLoads = 10);

    Material::SharedPtr resolveMaterialOverrideFromPath(const std::string &materialPath);
    Material::SharedPtr resolveRuntimeMeshMaterial(const CPUMesh &mesh);

private:
    bool looksLikeWindowsAbsolutePath(const std::string &path) const;
    std::filesystem::path makeAbsoluteNormalized(const std::filesystem::path &path) const;
    std::string resolveTexturePathForMaterial(const std::string &texturePath, const std::filesystem::path &materialAssetPath) const;
    Texture::SharedPtr loadTextureForMaterial(const std::string &texturePath, VkFormat format, const std::filesystem::path &materialAssetPath);
    std::string buildMaterialCacheKey(const CPUMaterial &materialCPU) const;
    std::string ensureCustomMaterialShaderPath(const CPUMaterial &materialCPU) const;
    Material::SharedPtr createMaterialFromCpuData(const CPUMaterial &materialCPU, const std::filesystem::path &materialAssetFilePath);
    bool consumeLoadBudget();

private:
    int m_newMaterialLoadsThisFrame{0};
    int m_maxNewMaterialLoadsPerFrame{10};
    std::unordered_map<std::string, Texture::SharedPtr> m_texturesByResolvedPath;
    std::unordered_set<std::string> m_failedTextureResolvedPaths;
    std::unordered_map<std::string, Material::SharedPtr> m_materialsByRuntimeKey;
    std::unordered_set<std::string> m_failedRuntimeMaterialKeys;
    std::unordered_map<std::string, Material::SharedPtr> m_materialsByAssetPath;
    std::unordered_set<std::string> m_failedMaterialAssetPaths;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCENE_MATERIAL_RESOLVER_HPP
