#ifndef ELIX_TEXTURE_HPP
#define ELIX_TEXTURE_HPP

#include "Core/Macros.hpp"
#include "Core/Image.hpp"
#include "Core/CommandPool.hpp"

#include <string>
#include <array>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Texture
{
public:
    using SharedPtr = std::shared_ptr<Texture>;

    Texture();

    void createFromPixels(uint32_t pixels = 0xFFFFFFFF, core::CommandPool::SharedPtr commandPool = nullptr);

    bool load(const std::string &path, core::CommandPool::SharedPtr commandPool = nullptr, bool freePixelsOnLoad = true);
    void freePixels();

    bool loadCubemap(const std::array<std::string, 6> &cubemaps, core::CommandPool::SharedPtr commandPool = nullptr, bool freePixelsOnLoad = true);

    void NewFunction(elix::core::Buffer::SPtr &buffer);

    bool loadHDR(const std::string &filepath);
    bool createCubemapFromHDR(const std::string &hdrPath, uint32_t cubemapSize = 512);
    bool createCubemapFromEquirectangular(const float *data, int width, int height, uint32_t cubemapSize = 512);

    unsigned char *getPixels() const;
    int getWidth() const;
    int getHeight() const;
    int getChannels() const;
    core::Image::SharedPtr getImage();
    VkImageView vkImageView();
    VkSampler vkSampler();

    ~Texture();

private:
    VkDevice m_device{VK_NULL_HANDLE};
    unsigned char *m_pixels{nullptr};
    int m_width{1};
    int m_height{1};
    int m_channels{0};
    core::Image::SharedPtr m_image{nullptr};
    VkImageView m_imageView{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TEXTURE_HPP