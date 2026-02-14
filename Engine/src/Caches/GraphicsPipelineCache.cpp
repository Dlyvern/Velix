#include "Engine/Caches/GraphicsPipelineCache.hpp"
#include <vector>
#include <fstream>
#include <stdexcept>
#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(cache)

VkPipelineCache GraphicsPipelineCache::getDeviceCache(VkDevice device)
{
    std::lock_guard<std::mutex> lock(s_mutex);

    auto it = s_graphicsPipelineCachePerDevice.find(device);
    if (it != s_graphicsPipelineCachePerDevice.end())
        return it->second;

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cacheInfo.initialDataSize = 0;
    cacheInfo.pInitialData = nullptr;

    VkPipelineCache cache;
    if (vkCreatePipelineCache(device, &cacheInfo, nullptr, &cache) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline cache");

    s_graphicsPipelineCachePerDevice[device] = cache;

    return cache;
}

void GraphicsPipelineCache::saveCacheToFile(VkDevice device, const std::string &path)
{
    VkPipelineCache cache = getDeviceCache(device);

    size_t dataSize = 0;
    vkGetPipelineCacheData(device, cache, &dataSize, nullptr);
    std::vector<uint8_t> cacheData(dataSize);
    vkGetPipelineCacheData(device, cache, &dataSize, cacheData.data());

    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(cacheData.data()), cacheData.size());
}

void GraphicsPipelineCache::loadCacheFromFile(VkDevice device, const std::string &path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file.is_open())
    {
        std::cerr << "Failed to load cache from file\n";
        return;
    }

    std::vector<uint8_t> cacheData((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());

    VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    cacheInfo.initialDataSize = cacheData.size();
    cacheInfo.pInitialData = cacheData.data();

    VkPipelineCache cache;
    if (vkCreatePipelineCache(device, &cacheInfo, nullptr, &cache) != VK_SUCCESS)
        throw std::runtime_error("Failed to load pipeline cache");

    std::lock_guard<std::mutex> lock(s_mutex);
    s_graphicsPipelineCachePerDevice[device] = cache;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
