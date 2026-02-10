#ifndef ELIX_SAMPLER_HPP
#define ELIX_SAMPLER_HPP

#include "Core/Macros.hpp"

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Sampler
{
    DECLARE_VK_HANDLE_METHODS(VkSampler)
    DECLARE_VK_SMART_PTRS(Sampler, VkSampler)
    ELIX_DECLARE_VK_LIFECYCLE()
public:
    Sampler(VkFilter magFilter, VkSamplerAddressMode addressModeU, VkBorderColor borderColor, VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS,
            VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, VkBool32 anisotropyEnable = VK_FALSE,
            float maxAnisotropy = 1.0f, VkBool32 unnormalizedCoordinates = VK_FALSE, VkBool32 compareEnable = VK_FALSE,
            float mipLodBias = 0.0f, float minLod = 0.0f, float maxLod = 1.0f);

    void createVk(VkFilter magFilter, VkSamplerAddressMode addressModeU, VkBorderColor borderColor, VkCompareOp compareOp = VK_COMPARE_OP_ALWAYS,
                  VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, VkBool32 anisotropyEnable = VK_FALSE,
                  float maxAnisotropy = 1.0f, VkBool32 unnormalizedCoordinates = VK_FALSE, VkBool32 compareEnable = VK_FALSE,
                  float mipLodBias = 0.0f, float minLod = 0.0f, float maxLod = 1.0f);

    void fuck()
    {
    }

    ~Sampler();
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SAMPLER_HPP