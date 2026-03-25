#ifndef ELIX_BINDLESS_REGISTRY_HPP
#define ELIX_BINDLESS_REGISTRY_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Engine/Material.hpp"
#include "Engine/Texture.hpp"

#include <unordered_map>
#include <vector>
#include <cstdint>

#include <vulkan/vulkan.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class BindlessRegistry
{
public:
    void initialize(VkDevice device);
    void cleanup(VkDevice device);

    /// Returns the bindless slot for a texture, registering it on first call.
    /// Falls back to the white-texture slot if tex is null.
    uint32_t getOrRegisterTexture(Texture *tex);

    /// Returns the material index for a material, registering it on first call.
    uint32_t getOrRegisterMaterial(Material *mat);

    /// Flushes accumulated material params to the GPU SSBO.
    void uploadMaterialParams(uint32_t usedMaterialSlots);

    VkDescriptorSet getBindlessSet() const { return m_bindlessSet; }
    core::Buffer *getMaterialParamsSSBO() { return m_materialParamsSSBO.get(); }
    std::vector<Material::GPUParams> &getCpuMaterialParams() { return m_cpuMaterialParams; }

    uint32_t getRegisteredMaterialCount() const { return m_nextMaterialSlot; }
    uint32_t getRegisteredTextureCount() const { return m_nextTextureSlot; }

    bool isInitialized() const { return m_bindlessSet != VK_NULL_HANDLE; }

private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkDescriptorPool m_bindlessPool{VK_NULL_HANDLE};
    VkDescriptorSet m_bindlessSet{VK_NULL_HANDLE};

    core::Buffer::SharedPtr m_materialParamsSSBO;
    std::vector<Material::GPUParams> m_cpuMaterialParams;

    std::unordered_map<Texture *, uint32_t> m_textureRegistry;
    std::unordered_map<Material *, uint32_t> m_materialRegistry;

    uint32_t m_nextTextureSlot{0};
    uint32_t m_nextMaterialSlot{0};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_BINDLESS_REGISTRY_HPP
