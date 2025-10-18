#ifndef ELIX_TEXTURE_IMAGE_HPP
#define ELIX_TEXTURE_IMAGE_HPP

#include "Core/Macros.hpp"
#include "Core/Image.hpp"
#include "Core/CommandPool.hpp"

#include <string>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class TextureImage
{
public:
    using SharedPtr = std::shared_ptr<TextureImage>;

    TextureImage();

    void create(VkDevice device, VkPhysicalDevice physicalDevice, core::CommandPool::SharedPtr commandPool, VkQueue queue, uint32_t pixels = 0xFFFFFFFF);

    bool load(VkDevice device, VkPhysicalDevice physicalDevice, const std::string& path, core::CommandPool::SharedPtr commandPool, VkQueue queue, bool freePixelsOnLoad = true);
    void freePixels();

    unsigned char* getPixels() const;
    int getWidth() const;
    int getHeight() const;
    int getChannels() const;

    core::Image<core::ImageDeleter>::SharedPtr getImage();
    VkImageView vkImageView();
    VkSampler vkSampler();

    ~TextureImage();
private:
    VkDevice m_device{VK_NULL_HANDLE};
    unsigned char* m_pixels{nullptr};
    int m_width{1};
    int m_height{1};
    int m_channels{0};
    core::Image<core::ImageDeleter>::SharedPtr m_image{nullptr};
    VkImageView m_imageView{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_TEXTURE_IMAGE_HPP