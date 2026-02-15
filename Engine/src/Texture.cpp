#include "Engine/Texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
// #define STB_IMAGE_STATIC
// #define STBI_ONLY_HDR
#include <stb_image.h>

#include <volk.h>

#include <iostream>
#include "Core/Buffer.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/VulkanContext.hpp"
#include <stdexcept>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Texture::Texture() = default;

bool Texture::loadHDR(const std::string &filepath)
{
    m_device = core::VulkanContext::getContext()->getDevice();

    int width, height, channels;
    float *data = stbi_loadf(filepath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!data)
    {
        std::cerr << "Failed to load HDR image: " << filepath << std::endl;
        std::cerr << "Reason: " << stbi_failure_reason() << std::endl;
        return false;
    }

    std::vector<float> imageData(width * height * 4);

    if (channels == 3)
    {
        for (int i = 0; i < width * height; ++i)
        {
            imageData[i * 4] = data[i * 3];
            imageData[i * 4 + 1] = data[i * 3 + 1];
            imageData[i * 4 + 2] = data[i * 3 + 2];
            imageData[i * 4 + 3] = 1.0f; // Alpha
        }
    }
    else
        memcpy(imageData.data(), data, width * height * 4 * sizeof(float));

    stbi_image_free(data);

    m_width = static_cast<uint32_t>(width);
    m_height = static_cast<uint32_t>(height);

    m_image = core::Image::createShared(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                        core::memory::MemoryUsage::GPU_ONLY, VK_FORMAT_R32G32B32A32_SFLOAT);

    auto commandPool = core::VulkanContext::getContext()->getTransferCommandPool();

    auto queue = core::VulkanContext::getContext()->getTransferQueue();

    VkDeviceSize imageSize = m_width * m_height * 4 * sizeof(float);

    auto stagingBuffer = core::Buffer::createShared(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        core::memory::MemoryUsage::CPU_TO_GPU);

    stagingBuffer->upload(imageData.data(), imageSize);

    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandPool, queue);
    m_image->copyBufferToImage(stagingBuffer, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), commandPool, queue);
    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandPool, queue);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image->vk();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture image view!");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(1);

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture sampler!");

    return true;
}

bool Texture::createCubemapFromHDR(const std::string &hdrPath, uint32_t cubemapSize)
{
    int width, height, channels;

    float *hdrData = stbi_loadf(hdrPath.c_str(), &width, &height, &channels, STBI_rgb);

    if (!hdrData)
    {
        std::cerr << "Failed to load HDR image: " << hdrPath << std::endl;
        return false;
    }

    bool result = createCubemapFromEquirectangular(hdrData, width, height, cubemapSize);

    stbi_image_free(hdrData);

    return result;
}

bool Texture::createCubemapFromEquirectangular(const float *data, int width, int height, uint32_t cubemapSize)
{
    m_device = core::VulkanContext::getContext()->getDevice();

    m_width = cubemapSize;
    m_height = cubemapSize;

    m_image = core::Image::createShared(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height),
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        core::memory::MemoryUsage::GPU_ONLY, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                        6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    std::vector<std::vector<float>> faces(6);

    glm::vec3 faceDirections[6][3] = {
        {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}}, // +X (right)
        {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}}, // -X (left)
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},   // +Y (top)
        {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}}, // -Y (bottom)
        {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}},  // +Z (front)
        {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}} // -Z (back)
    };

    auto commandPool = core::VulkanContext::getContext()->getTransferCommandPool();

    auto queue = core::VulkanContext::getContext()->getTransferQueue();

    m_image->transitionImageLayout(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   commandPool, queue, 6);

    vkQueueWaitIdle(queue);

    // Generate each face
    for (int face = 0; face < 6; ++face)
    {
        faces[face].resize(cubemapSize * cubemapSize * 4); // RGBA

        for (uint32_t y = 0; y < cubemapSize; ++y)
        {
            for (uint32_t x = 0; x < cubemapSize; ++x)
            {
                // Convert to normalized coordinates [-1, 1]
                float u = (2.0f * (x + 0.5f) / cubemapSize) - 1.0f;
                float v = (2.0f * (y + 0.5f) / cubemapSize) - 1.0f;

                // Calculate direction vector
                glm::vec3 direction =
                    faceDirections[face][0] +
                    u * faceDirections[face][1] +
                    v * faceDirections[face][2];

                direction = glm::normalize(direction);

                // Convert to spherical coordinates
                float phi = atan2(direction.z, direction.x);
                float theta = acos(glm::clamp(direction.y, -1.0f, 1.0f));

                // Convert to UV coordinates
                float u_hdr = phi / (2.0f * glm::pi<float>()) + 0.5f;
                float v_hdr = theta / glm::pi<float>();

                // Sample equirectangular map (with bilinear filtering)
                float x_hdr = u_hdr * width;
                float y_hdr = v_hdr * height;

                int x0 = static_cast<int>(floor(x_hdr));
                int y0 = static_cast<int>(floor(y_hdr));
                int x1 = (x0 + 1) % width;
                int y1 = glm::min(y0 + 1, height - 1);

                float tx = x_hdr - x0;
                float ty = y_hdr - y0;

                // Bilinear interpolation
                glm::vec3 color00, color01, color10, color11;

                for (int c = 0; c < 3; ++c)
                {
                    color00[c] = data[(y0 * width + x0) * 3 + c];
                    color01[c] = data[(y0 * width + x1) * 3 + c];
                    color10[c] = data[(y1 * width + x0) * 3 + c];
                    color11[c] = data[(y1 * width + x1) * 3 + c];
                }

                glm::vec3 color0 = glm::mix(color00, color01, tx);
                glm::vec3 color1 = glm::mix(color10, color11, tx);
                glm::vec3 finalColor = glm::mix(color0, color1, ty);

                int idx = (y * cubemapSize + x) * 4;
                faces[face][idx] = finalColor.r;
                faces[face][idx + 1] = finalColor.g;
                faces[face][idx + 2] = finalColor.b;
                faces[face][idx + 3] = 1.0f;
            }
        }

        VkDeviceSize faceSize = cubemapSize * cubemapSize * 4 * sizeof(float);

        auto stagingBuffer = core::Buffer::createShared(
            faceSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);

        stagingBuffer->upload(faces[face].data(), faceSize);

        m_image->copyBufferToImage(stagingBuffer, cubemapSize, cubemapSize, commandPool, queue, 1, face);

        vkQueueWaitIdle(queue);
    }

    m_image->transitionImageLayout(
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        commandPool, queue, 6);

    vkQueueWaitIdle(queue);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image->vk();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create cubemap image view!");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(1);

    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create cubemap sampler!");

    return true;
}

void Texture::createFromPixels(uint32_t pixels, core::CommandPool::SharedPtr commandPool)
{
    m_width = 1;
    m_height = 1;
    m_device = core::VulkanContext::getContext()->getDevice();

    VkDeviceSize imageSize = sizeof(pixels);

    auto buffer = core::Buffer::createShared(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

    buffer->upload(&pixels, imageSize);

    m_image = core::Image::createShared(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, core::memory::MemoryUsage::GPU_ONLY);

    if (!commandPool)
        commandPool = core::VulkanContext::getContext()->getTransferCommandPool();

    auto queue = core::VulkanContext::getContext()->getTransferQueue();

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

    if (VkResult result = vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView); result != VK_SUCCESS)
        throw std::runtime_error("Failed to create image view: " + core::helpers::vulkanResultToString(result));

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    //! TODO Fix it later
    samplerInfo.maxAnisotropy = core::VulkanContext::getContext()->getPhysicalDevicePoperties().limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (VkResult result = vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler); result != VK_SUCCESS)
        throw std::runtime_error("Failed to create sampler: " + core::helpers::vulkanResultToString(result));
}

bool Texture::load(const std::string &path, core::CommandPool::SharedPtr commandPool, bool freePixelsOnLoad)
{
    m_device = core::VulkanContext::getContext()->getDevice();

    m_pixels = stbi_load(path.c_str(), &m_width, &m_height, &m_channels, STBI_rgb_alpha);

    VkDeviceSize imageSize = m_width * m_height * 4;

    if (!m_pixels)
    {
        std::cerr << "Failed to load image: " << path << std::endl;
        return false;
    }

    auto buffer = core::Buffer::createShared(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

    buffer->upload(m_pixels, imageSize);

    if (freePixelsOnLoad)
        freePixels();

    if (!commandPool)
        commandPool = core::VulkanContext::getContext()->getTransferCommandPool();

    m_image = core::Image::createShared(static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, core::memory::MemoryUsage::GPU_ONLY);

    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandPool, core::VulkanContext::getContext()->getGraphicsQueue());
    m_image->copyBufferToImage(buffer, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), commandPool, core::VulkanContext::getContext()->getGraphicsQueue());
    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandPool, core::VulkanContext::getContext()->getGraphicsQueue());

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image->vk();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image view");

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    //! TODO Fix it later
    samplerInfo.maxAnisotropy = core::VulkanContext::getContext()->getPhysicalDevicePoperties().limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (VkResult result = vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler); result != VK_SUCCESS)
    {
        std::string errorMsg = "Failed to create sampler: ";

        errorMsg += core::helpers::vulkanResultToString(result);

        throw std::runtime_error(errorMsg);
    }

    return true;
}

// TODO does not work
bool Texture::loadCubemap(const std::array<std::string, 6> &cubemaps,
                          core::CommandPool::SharedPtr commandPool, bool freePixelsOnLoad)
{
    m_device = core::VulkanContext::getContext()->getDevice();

    std::array<stbi_uc *, 6> facePixels{};

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

    auto buffer = core::Buffer::createShared(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

    void *data;

    buffer->map(data);

    for (int face = 0; face < 6; ++face)
    {
        size_t offset = face * layerSize;

        memcpy(static_cast<char *>(data) + offset, facePixels[face], layerSize);

        if (freePixelsOnLoad)
            stbi_image_free(facePixels[face]);
    }

    buffer->unmap();

    commandPool = core::VulkanContext::getContext()->getGraphicsCommandPool();

    m_image = core::Image::createShared(static_cast<uint32_t>(m_width),
                                        static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, core::memory::MemoryUsage::GPU_ONLY, VK_FORMAT_R8G8B8A8_SRGB,
                                        VK_IMAGE_TILING_OPTIMAL, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   commandPool, core::VulkanContext::getContext()->getGraphicsQueue(), 6);
    m_image->copyBufferToImage(buffer, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), commandPool, core::VulkanContext::getContext()->getGraphicsQueue(), 6);
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
    imageViewCI.subresourceRange.layerCount = 6;

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

VkSampler Texture::vkSampler()
{
    return m_sampler;
}

VkImageView Texture::vkImageView()
{
    return m_imageView;
}

core::Image::SharedPtr Texture::getImage()
{
    return m_image;
}

Texture::~Texture()
{
    if (m_imageView)
        vkDestroyImageView(m_device, m_imageView, nullptr);
    if (m_sampler)
        vkDestroySampler(m_device, m_sampler, nullptr);
}

unsigned char *Texture::getPixels() const
{
    return m_pixels;
}

int Texture::getWidth() const
{
    return m_width;
}

int Texture::getHeight() const
{
    return m_height;
}

int Texture::getChannels() const
{
    return m_channels;
}

void Texture::freePixels()
{
    if (!m_pixels)
        return;

    stbi_image_free(m_pixels);
    m_pixels = nullptr;
}

ELIX_NESTED_NAMESPACE_END