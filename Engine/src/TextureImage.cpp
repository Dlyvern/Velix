#include "Engine/TextureImage.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <volk.h>

#include <iostream>
#include "Core/Buffer.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/VulkanContext.hpp"
#include <stdexcept>
ELIX_NESTED_NAMESPACE_BEGIN(engine)

TextureImage::TextureImage() = default;

void TextureImage::create(VkDevice device, core::CommandPool::SharedPtr commandPool, VkQueue queue, uint32_t pixels)
{
    m_width = 1;
    m_height = 1;
    m_device = device;

    VkDeviceSize imageSize = sizeof(pixels);

    auto buffer = core::Buffer::create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    buffer->upload(static_cast<const void*>(&pixels), imageSize);

    m_image = std::make_shared<core::Image>(m_device, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
    VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandPool, queue);
    m_image->copyBufferToImage(buffer, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), commandPool, queue);
    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandPool, queue);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image->vk();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if(vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image view");

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    //!TODO Fix it later
    samplerInfo.maxAnisotropy = core::VulkanContext::getContext()->getPhysicalDevicePoperties().limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;

    if(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create sampler");
}

bool TextureImage::load(VkDevice device, const std::string& path, core::CommandPool::SharedPtr commandPool, VkQueue queue, bool freePixelsOnLoad)
{
    m_device = device;

    m_pixels = stbi_load(path.c_str(), &m_width, &m_height, &m_channels, STBI_rgb_alpha);

    VkDeviceSize imageSize = m_width * m_height * 4;

    if(!m_pixels)
    {
        std::cerr << "Failed to load image: " << path << std::endl;
        return false;
    }

    auto buffer = core::Buffer::create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    buffer->upload(m_pixels, imageSize);

    if(freePixelsOnLoad)
        freePixels();

    m_image = std::make_shared<core::Image>(m_device, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
    VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandPool, queue);
    m_image->copyBufferToImage(buffer, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), commandPool, queue);
    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandPool, queue);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image->vk();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if(vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image view");

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    //!TODO Fix it later
    samplerInfo.maxAnisotropy = core::VulkanContext::getContext()->getPhysicalDevicePoperties().limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;

    if(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create sampler");
    
}

VkSampler TextureImage::vkSampler()
{
    return m_sampler;
}

VkImageView TextureImage::vkImageView()
{
    return m_imageView;
}

core::Image::SharedPtr TextureImage::getImage()
{
    return m_image;
}

TextureImage::~TextureImage()
{
    if(m_imageView)
        vkDestroyImageView(m_device, m_imageView, nullptr);
    if(m_sampler)
        vkDestroySampler(m_device, m_sampler, nullptr);
}

unsigned char* TextureImage::getPixels() const
{
    return m_pixels;
}

int TextureImage::getWidth() const
{
    return m_width;
}

int TextureImage::getHeight() const
{
    return m_height;
}

int TextureImage::getChannels() const
{
    return m_channels;
}

void TextureImage::freePixels()
{
    if(!m_pixels)
        return;

    stbi_image_free(m_pixels);
    m_pixels = nullptr;
}

ELIX_NESTED_NAMESPACE_END
