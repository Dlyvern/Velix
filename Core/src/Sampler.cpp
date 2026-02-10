#include "Core/Sampler.hpp"
#include "Core/VulkanContext.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

Sampler::Sampler(VkFilter magFilter, VkSamplerAddressMode addressModeU, VkBorderColor borderColor, VkCompareOp compareOp,
                 VkSamplerMipmapMode mipmapMode, VkBool32 anisotropyEnable, float maxAnisotropy,
                 VkBool32 unnormalizedCoordinates, VkBool32 compareEnable, float mipLodBias, float minLod, float maxLod)
{
    createVk(magFilter, addressModeU, borderColor, compareOp, mipmapMode, anisotropyEnable, maxAnisotropy,
             unnormalizedCoordinates, compareEnable, mipLodBias, minLod, maxLod);
}

void Sampler::createVk(VkFilter magFilter, VkSamplerAddressMode addressModeU, VkBorderColor borderColor, VkCompareOp compareOp,
                       VkSamplerMipmapMode mipmapMode, VkBool32 anisotropyEnable, float maxAnisotropy,
                       VkBool32 unnormalizedCoordinates, VkBool32 compareEnable, float mipLodBias, float minLod, float maxLod)
{
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = magFilter;
    samplerInfo.minFilter = magFilter;
    samplerInfo.addressModeU = addressModeU;
    samplerInfo.addressModeV = addressModeU;
    samplerInfo.addressModeW = addressModeU;
    samplerInfo.anisotropyEnable = anisotropyEnable;
    samplerInfo.maxAnisotropy = maxAnisotropy;
    samplerInfo.borderColor = borderColor;
    samplerInfo.unnormalizedCoordinates = unnormalizedCoordinates;
    samplerInfo.compareEnable = compareEnable;
    samplerInfo.compareOp = compareOp;
    samplerInfo.mipmapMode = mipmapMode;
    samplerInfo.mipLodBias = mipLodBias;
    samplerInfo.minLod = minLod;
    samplerInfo.maxLod = maxLod;

    if (vkCreateSampler(core::VulkanContext::getContext()->getDevice(), &samplerInfo, nullptr, &m_handle) != VK_SUCCESS)
        throw std::runtime_error("Failed to create a sample");
}

void Sampler::destroyVkImpl()
{
    if (m_handle)
    {
        vkDestroySampler(core::VulkanContext::getContext()->getDevice(), m_handle, nullptr);
        m_handle = VK_NULL_HANDLE;
    }
}

Sampler::~Sampler()
{
    destroyVk();
}

ELIX_NESTED_NAMESPACE_END
