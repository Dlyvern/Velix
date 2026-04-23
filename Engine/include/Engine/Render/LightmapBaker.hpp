#pragma once

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Core/Image.hpp"
#include "Core/Sampler.hpp"
#include "Core/PipelineLayout.hpp"
#include "Core/DescriptorSetLayout.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Scene;

namespace renderGraph
{
class ShadowRenderGraphPass;
class RenderGraph;
}

struct LightmapBakeSettings
{
    uint32_t resolution{1024};
    bool dilate{true};
    uint32_t dilateRadius{2};
};

struct LightmapBakeProgress
{
    std::atomic<uint32_t> completedEntities{0};
    std::atomic<uint32_t> totalEntities{0};
    std::atomic<bool>     done{true};
    std::string           currentEntityName;
    std::mutex            statusMutex;
};

class LightmapBaker
{
public:
    LightmapBaker() = default;
    ~LightmapBaker();




    void bake(engine::Scene *scene,
              renderGraph::ShadowRenderGraphPass *shadowPass,
              renderGraph::RenderGraph *renderGraph,
              const std::string &outputDir,
              const LightmapBakeSettings &settings,
              LightmapBakeProgress &progress);

private:
    struct BakeTarget
    {
        VkImage       image{VK_NULL_HANDLE};
        VkImageView   imageView{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
    };

    void createPipeline(uint32_t resolution);
    void destroyPipeline();

    BakeTarget createBakeTarget(uint32_t resolution);
    void destroyBakeTarget(BakeTarget &target);


    std::vector<uint8_t> readbackImage(VkImage image, uint32_t width, uint32_t height);


    void dilate(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height, uint32_t radius);

    void createShadowDescriptorSet(VkImageView dirView, VkImageView spotView, VkImageView cubeView,
                                   VkSampler shadowSampler);


    VkRenderPass             m_bakeRenderPass{VK_NULL_HANDLE};
    VkPipeline               m_bakePipeline{VK_NULL_HANDLE};
    VkPipelineLayout         m_bakePipelineLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout    m_bakeSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool         m_bakeDescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet          m_shadowDescriptorSet{VK_NULL_HANDLE};
    core::Sampler::SharedPtr m_shadowSampler;

    bool m_pipelineInitialised{false};
    uint32_t m_lastResolution{0};
};

ELIX_NESTED_NAMESPACE_END
