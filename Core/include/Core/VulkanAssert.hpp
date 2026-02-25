#ifndef ELIX_VULKAN_ASSERT_HPP
#define ELIX_VULKAN_ASSERT_HPP

#include "Core/Assert.hpp"
#include "Core/VulkanHelpers.hpp"

#include <stdexcept>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(core)

inline std::string buildVulkanErrorMessage(const char *vkCall, VkResult result, const char *context = nullptr)
{
    std::string message;
    message.reserve(256);

    message += vkCall ? vkCall : "<null Vulkan call>";
    message += " failed with ";
    message += helpers::vulkanResultToString(result);
    message += " (";
    message += std::to_string(static_cast<int>(result));
    message += ")";

    if (context && context[0] != '\0')
    {
        message += " | ";
        message += context;
    }

    return message;
}

inline void reportVulkanError(const char *vkCall, VkResult result, const char *file, int line, const char *func, const char *context = nullptr)
{
    const std::string message = buildVulkanErrorMessage(vkCall, result, context);
    AssertReportFmt(vkCall, file, line, func, AssertAction::LogOnly, "%s", message.c_str());
}

[[noreturn]] inline void throwVulkanError(const char *vkCall, VkResult result, const char *file, int line, const char *func, const char *context = nullptr)
{
    const std::string message = buildVulkanErrorMessage(vkCall, result, context);
    AssertReportFmt(vkCall, file, line, func, AssertAction::LogOnly, "%s", message.c_str());
    throw std::runtime_error(message);
}

ELIX_NESTED_NAMESPACE_END

#define VX_VK_CHECK(vkCall)                                                                                                           \
    do                                                                                                                                \
    {                                                                                                                                 \
        const VkResult _vx_vk_result = (vkCall);                                                                                     \
        if (_vx_vk_result != VK_SUCCESS)                                                                                              \
            ::elix::core::throwVulkanError(#vkCall, _vx_vk_result, __FILE__, __LINE__, VX_FUNC_SIG);                                \
    } while (0)

#define VX_VK_CHECK_MSG(vkCall, contextExpr)                                                                                          \
    do                                                                                                                                \
    {                                                                                                                                 \
        const VkResult _vx_vk_result = (vkCall);                                                                                     \
        if (_vx_vk_result != VK_SUCCESS)                                                                                              \
        {                                                                                                                             \
            const std::string _vx_vk_context = (contextExpr);                                                                        \
            ::elix::core::throwVulkanError(#vkCall, _vx_vk_result, __FILE__, __LINE__, VX_FUNC_SIG, _vx_vk_context.c_str());       \
        }                                                                                                                             \
    } while (0)

#define VX_VK_TRY(vkCall)                                                                                                             \
    ([&]() -> VkResult                                                                                                                \
     {                                                                                                                                \
         const VkResult _vx_vk_result = (vkCall);                                                                                    \
         if (_vx_vk_result != VK_SUCCESS)                                                                                             \
             ::elix::core::reportVulkanError(#vkCall, _vx_vk_result, __FILE__, __LINE__, VX_FUNC_SIG);                             \
         return _vx_vk_result;                                                                                                        \
     }())

#define VX_VK_TRY_MSG(vkCall, contextExpr)                                                                                            \
    ([&]() -> VkResult                                                                                                                \
     {                                                                                                                                \
         const VkResult _vx_vk_result = (vkCall);                                                                                    \
         if (_vx_vk_result != VK_SUCCESS)                                                                                             \
         {                                                                                                                            \
             const std::string _vx_vk_context = (contextExpr);                                                                       \
             ::elix::core::reportVulkanError(#vkCall, _vx_vk_result, __FILE__, __LINE__, VX_FUNC_SIG, _vx_vk_context.c_str());     \
         }                                                                                                                            \
         return _vx_vk_result;                                                                                                        \
     }())

#endif // ELIX_VULKAN_ASSERT_HPP
