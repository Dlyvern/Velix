#ifndef ELIX_TEXTURE_HPP
#define ELIX_TEXTURE_HPP

#include "Core/Macros.hpp"
#include "Core/Image.hpp"
#include "Core/CommandPool.hpp"
#include "Core/Sampler.hpp"

#include <string>
#include <array>
#include <memory>
#include <cstddef>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Texture
{
public:
    using SharedPtr = std::shared_ptr<Texture>;

    Texture();

    static uint32_t packRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    void createFromPixels(uint32_t pixels = 0xFFFFFFFF, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    bool createFromMemory(const void *pixels, size_t byteCount, uint32_t width, uint32_t height, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB, uint32_t channels = 4);

    void freePixels();

    bool loadCubemap(const std::array<std::string, 6> &cubemaps);

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

    void destroy();

    static void createDefaults();
    static void destroyDefaults();
    static SharedPtr getDefaultWhiteTexture();
    static SharedPtr getDefaultNormalTexture();
    static SharedPtr getDefaultOrmTexture();
    static SharedPtr getDefaultBlackTexture();

private:
    VkDevice m_device{VK_NULL_HANDLE};
    unsigned char *m_pixels{nullptr};
    int m_width{1};
    int m_height{1};
    int m_channels{0};
    core::Image::SharedPtr m_image{nullptr};
    VkImageView m_imageView{VK_NULL_HANDLE};
    core::Sampler::SharedPtr m_sampler{nullptr};

    static inline SharedPtr s_whiteTexture{nullptr};
    static inline SharedPtr s_normalTexture{nullptr};
    static inline SharedPtr s_ormTexture{nullptr};
    static inline SharedPtr s_blackTexture{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TEXTURE_HPP
