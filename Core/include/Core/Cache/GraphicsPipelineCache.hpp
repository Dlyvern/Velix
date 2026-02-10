#ifndef ELIX_GRAPHICS_PIPELINE_CACHE_HPP
#define ELIX_GRAPHICS_PIPELINE_CACHE_HPP

#include "Core/Macros.hpp"

#include <volk.h>
#include <unordered_map>
#include <mutex>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(cache)

class GraphicsPipelineCache
{
public:
    static VkPipelineCache getDeviceCache(VkDevice device);
    static void saveCacheToFile(VkDevice device, const std::string &path);
    static void loadCacheFromFile(VkDevice device, const std::string &path);

private:
    static inline std::unordered_map<VkDevice, VkPipelineCache> s_graphicsPipelineCachePerDevice;
    static inline std::mutex s_mutex;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GRAPHICS_PIPELINE_CACHE_HPP