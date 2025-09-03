#ifndef RENDER_HPP
#define RENDER_HPP

#include "Macros.hpp"
#include "Core/VulkanContext.hpp"

#include <vector>
#include <cstdint>

ELIX_NAMESPACE_BEGIN

class Render
{
public:
    void init(VkPipelineShaderStageCreateInfo* shadersShit);

    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    void drawFrame();

    void cleanup();
private:
    void createSyncObjects();

    std::vector<VkFramebuffer> swapChainFramebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkRenderPass renderPass;
    VkPipeline graphicsPipeline;
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};


    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;
};

ELIX_NAMESPACE_END

#endif //RENDER_HPP