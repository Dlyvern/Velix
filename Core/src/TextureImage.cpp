#include "Core/TextureImage.hpp"

// #define STB_IMAGE_IMPLEMENTATION
// #include <stb_image.h>

#include <volk.h>

#include <iostream>
#include "Core/Buffer.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

bool TextureImage::load(const std::string& path, bool freeOnLoad)
{
    int width, height, channels;

    void* pixels;

    // stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    VkDeviceSize imageSize = width * height * 4;

    if(!pixels)
    {
        std::cerr << "Failed to load image: " << path << std::endl;
        return false;
    }

    auto buffer = Buffer::create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    buffer->upload(pixels, imageSize);

    if(freeOnLoad)
        free();

    return true;
}

void TextureImage::free()
{
    // stbi_image_free(pixels);
}

ELIX_NESTED_NAMESPACE_END
