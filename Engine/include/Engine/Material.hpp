#ifndef ELIX_MATERIAL_HPP
#define ELIX_MATERIAL_HPP

#include "Core/Macros.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include "Engine/TextureImage.hpp"

#include <cstdint>

#include <glm/mat4x4.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Material
{
public:
    using SharedPtr = std::shared_ptr<Material>;

    Material(VkDevice device, VkPhysicalDevice physicalDevice, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout);

    void updateDescriptorSets();

    VkDescriptorSet getDescriptorSet(uint32_t frameIndex) const;

    void setTexture(TextureImage::SharedPtr texture);
    void setColor(const glm::vec4& color);

    const glm::vec4& getColor() const;
    TextureImage::SharedPtr getTexture() const;

    static Material::SharedPtr getDefaultMaterial()
    {
        return s_defaultMaterial;
    }

    static void createDefaultMaterial(VkDevice device, VkPhysicalDevice physicalDevice, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout);
    static SharedPtr create(VkDevice device, VkPhysicalDevice physicalDevice, VkDescriptorPool descriptorPool, uint32_t maxFramesInFlight, engine::TextureImage::SharedPtr texture, core::DescriptorSetLayout::SharedPtr descriptorSetLayout);
private:
    uint32_t m_maxFramesInFlight;
    std::vector<VkDescriptorSet> m_descriptorSets;
    engine::TextureImage::SharedPtr m_texture{nullptr};
    VkDevice m_device{VK_NULL_HANDLE};
    std::vector<core::Buffer::SharedPtr> m_colorBuffers;
    static inline Material::SharedPtr s_defaultMaterial{nullptr};
    glm::vec4 m_color{1.0f};

    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    core::DescriptorSetLayout::SharedPtr m_descriptorSetLayout{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_MATERIAL_HPP