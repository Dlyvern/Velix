#ifndef ELIX_PUSH_CONSTANT_HPP
#define ELIX_PUSH_CONSTANT_HPP

#include "Core/Macros.hpp"

#include <volk.h>

#include <memory>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

template<typename T>
class PushConstant
{
public:
    static constexpr uint32_t size = sizeof(T);
    static constexpr uint32_t allignedSize =  (size + 3) & ~3; //*Vulkan requires push constant sizes to be multiple of 4 bytes

    //TODO getDevice()->gpuProps.limits.maxPushConstantsSize;
    static VkPushConstantRange getRange(VkShaderStageFlags stageFlags, uint32_t offset = 0)
    {
        VkPushConstantRange range{};
        range.stageFlags = stageFlags;
        range.offset = offset;
        range.size = allignedSize;

        return range;
    }
};

ELIX_NESTED_NAMESPACE_END


#endif //ELIX_PUSH_CONSTANT_HPP