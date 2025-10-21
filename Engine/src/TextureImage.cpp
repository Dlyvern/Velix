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

void TextureImage::create(VkDevice device, VkPhysicalDevice physicalDevice, core::CommandPool::SharedPtr commandPool, VkQueue queue, uint32_t pixels)
{
    m_width = 1;
    m_height = 1;
    m_device = device;

    VkDeviceSize imageSize = sizeof(pixels);

    auto buffer = core::Buffer::create(device, physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    buffer->upload(&pixels, imageSize);

    m_image = core::Image<core::ImageDeleter>::create(m_device, physicalDevice, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
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

    if(VkResult result = vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler); result != VK_SUCCESS)
    {
        std::string errorMsg = "Failed to create sampler: ";

        errorMsg += core::helpers::vulkanResultToString(result);

        throw std::runtime_error(errorMsg);
    }
}

bool TextureImage::load(VkDevice device, VkPhysicalDevice physicalDevice, const std::string& path, core::CommandPool::SharedPtr commandPool, VkQueue queue, bool freePixelsOnLoad)
{
    m_device = device;

    m_pixels = stbi_load(path.c_str(), &m_width, &m_height, &m_channels, STBI_rgb_alpha);

    VkDeviceSize imageSize = m_width * m_height * 4;

    if(!m_pixels)
    {
        std::cerr << "Failed to load image: " << path << std::endl;
        return false;
    }

    auto buffer = core::Buffer::create(device, physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    buffer->upload(m_pixels, imageSize);

    if(freePixelsOnLoad)
        freePixels();

    m_image = core::Image<core::ImageDeleter>::create(m_device, physicalDevice, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
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

    if(VkResult result = vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler); result != VK_SUCCESS)
    {
        std::string errorMsg = "Failed to create sampler: ";

        errorMsg += core::helpers::vulkanResultToString(result);

        throw std::runtime_error(errorMsg);
    }
    
    return true;
}

bool TextureImage::loadCubemap(VkDevice device, VkPhysicalDevice physicalDevice, const std::array<std::string, 6>& cubemaps, 
core::CommandPool::SharedPtr commandPool, VkQueue queue, bool freePixelsOnLoad)
{
    m_device = device;
    
    std::array<stbi_uc*, 6> facePixels{};

    for (int i = 0; i < 6; ++i) 
    {
        facePixels[i] = stbi_load(cubemaps[i].c_str(), &m_width, &m_height, &m_channels, STBI_rgb_alpha);

        if (!facePixels[i])
        {
            for (int j = 0; j < i; ++j)
                stbi_image_free(facePixels[j]);

            throw std::runtime_error("failed to load texture image: " + cubemaps[i]);
        }
        
        if (i > 0 && (m_width != m_height))
        {
            for (int j = 0; j <= i; ++j)
                stbi_image_free(facePixels[j]);

            throw std::runtime_error("cubemap faces must have same dimensions");
        }
    }


    VkDeviceSize imageSize = m_width * m_height * 4;
    VkDeviceSize layerSize = imageSize;
    VkDeviceSize totalSize = layerSize * 6;

    auto buffer = core::Buffer::create(device, physicalDevice, totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | 
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    
    void* data;

    vkMapMemory(device, buffer->vkDeviceMemory(), 0, totalSize, 0, &data);

    for(int face = 0; face < 6; ++face)
    {
        size_t offset = face * layerSize;

        memcpy(static_cast<char*>(data) + offset, facePixels[face], layerSize);

        if(freePixelsOnLoad)
            stbi_image_free(facePixels[face]);
    }

    VkImageCreateInfo imageCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.extent.width = static_cast<uint32_t>(m_width);
    imageCI.extent.height = static_cast<uint32_t>(m_height);
    imageCI.extent.depth = 1;
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 6;
    imageCI.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    // if(vkCreateImage(device, &imageCI, nullptr, ) != VK_SUCCESS)
    //     throw std::runtime_error("Failed to create cubemap image");

    // VkMemoryRequirements memRequirements;
    // vkGetImageMemoryRequirements(m_device, m_image, &memRequirements);

    // VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    // allocInfo.allocationSize = memRequirements.size;
    // allocInfo.memoryTypeIndex = core::helpers::findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);
    
    // if(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_imageMemory) != VK_SUCCESS)
    //     throw std::runtime_error("Failed to allocate image memory");
    
    // vkBindImageMemory(m_device, m_image, m_imageMemory, 0);

    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
    commandPool, core::VulkanContext::getContext()->getGraphicsQueue(), 6);
    m_image->copyBufferToImage(buffer, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), commandPool, queue);
    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    commandPool, core::VulkanContext::getContext()->getGraphicsQueue(), 6);


    VkImageViewCreateInfo imageViewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    imageViewCI.image = m_image->vk();
    imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    imageViewCI.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageViewCI.subresourceRange.baseMipLevel = 0;
    imageViewCI.subresourceRange.levelCount = 1;
    imageViewCI.subresourceRange.baseArrayLayer = 0;
    imageViewCI.subresourceRange.layerCount = 6; // All 6 faces

    if (vkCreateImageView(m_device, &imageViewCI, nullptr, &m_imageView) != VK_SUCCESS)
        throw std::runtime_error("failed to create cubemap image view!");

    VkSamplerCreateInfo samplerCI{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.anisotropyEnable = VK_TRUE;
    samplerCI.maxAnisotropy = 16.0f;
    samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerCI.unnormalizedCoordinates = VK_FALSE;
    samplerCI.compareEnable = VK_FALSE;
    samplerCI.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.mipLodBias = 0.0f;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 0.0f;

    if (vkCreateSampler(m_device, &samplerCI, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("failed to create cubemap sampler!");
    
    return true;
}


VkSampler TextureImage::vkSampler()
{
    return m_sampler;
}

VkImageView TextureImage::vkImageView()
{
    return m_imageView;
}

core::Image<core::ImageDeleter>::SharedPtr TextureImage::getImage()
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
