#ifndef ELIX_MATERIAL_HPP
#define ELIX_MATERIAL_HPP

#include "Core/Macros.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include "Engine/TextureImage.hpp"

#include <cstdint>

#include <glm/mat4x4.hpp>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class CPUMaterial
{
public:
    std::string albedoTexture;
    glm::vec4 color;
    std::string name;
};

class Material
{
public:
    using SharedPtr = std::shared_ptr<Material>;

    Material(VkDescriptorPool descriptorPool, engine::TextureImage::SharedPtr texture);

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

    static void createDefaultMaterial(VkDescriptorPool descriptorPool, engine::TextureImage::SharedPtr texture);
    static void deleteDefaultMaterial();
    static SharedPtr create(VkDescriptorPool descriptorPool, engine::TextureImage::SharedPtr texture);
private:
    uint32_t m_maxFramesInFlight;
    std::vector<VkDescriptorSet> m_descriptorSets;
    engine::TextureImage::SharedPtr m_texture{nullptr};
    VkDevice m_device{VK_NULL_HANDLE};
    std::vector<core::Buffer::SharedPtr> m_colorBuffers;
    static inline Material::SharedPtr s_defaultMaterial{nullptr};
    glm::vec4 m_color{1.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_MATERIAL_HPP