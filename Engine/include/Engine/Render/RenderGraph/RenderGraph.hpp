#ifndef ELIX_RENDER_GRAPH_HPP
#define ELIX_RENDER_GRAPH_HPP

#include "Core/Macros.hpp"
#include "Core/SwapChain.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/CommandPool.hpp"
#include "Core/DescriptorPool.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Camera.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesBuilder.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesCompiler.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesStorage.hpp"
#include "Engine/Render/RenderGraph/RenderGraphProfilingData.hpp"

#include <typeindex>
#include <unordered_map>
#include <vector>
#include <queue>
#include <string>
#include <cstdint>
#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RenderGraph
{
    struct RenderGraphPassData
    {
        IRenderGraphPass::SharedPtr renderGraphPass{nullptr};
        uint32_t id{0};
        RGPPassInfo passInfo;
        uint32_t indegree{0};           //*How many passes should run before me
        std::vector<uint32_t> outgoing; //*Which passes depend on me(Their ids)
    };

public:
    RenderGraph(VkDevice device, core::SwapChain::SharedPtr swapchain);

    template <typename T, typename... Args>
    T *addPass(Args &&...args)
    {
        static_assert(!std::is_abstract_v<T>, "RenderGraph::addPass() Cannot add abstract render pass!");
        static_assert(std::is_base_of_v<IRenderGraphPass, T>, "RenderGraph::addPass() T must derive from IRenderGraphPass class");

        const auto type = std::type_index(typeid(T));
        auto renderPass = std::make_shared<T>(std::forward<Args>(args)...);
        T *ptr = renderPass.get();

        RenderGraphPassData renderGraphPassInfo{};
        renderGraphPassInfo.renderGraphPass = std::move(renderPass);
        renderGraphPassInfo.id = m_renderGraphPasses.size(); //! For static ok, for dynamic - no

        m_renderGraphPasses[type] = renderGraphPassInfo;

        return ptr;
    }

    void prepareFrame(Camera::SharedPtr camera, Scene *scene, float deltaTime);
    void addAdditionalFrameData(const std::vector<AdditionalPerFrameData> &data)
    {
        if (data.empty())
            return;

        m_perFrameData.additionalData.insert(m_perFrameData.additionalData.begin(), data.begin(), data.end());
    }
    void draw();
    void setup();

    const RenderGraphFrameProfilingData &getLastFrameProfilingData() const
    {
        return m_lastFrameProfilingData;
    }

    static constexpr uint16_t MAX_FRAMES_IN_FLIGHT = 2;

    VkDescriptorPool getDescriptorPool() const
    {
        return m_descriptorPool;
    }

    void createRenderGraphResources();

    void cleanResources();

private:
    RenderGraphPassData *findRenderGraphPassById(uint32_t id)
    {
        for (auto &[_, renderGraphPass] : m_renderGraphPasses)
            if (renderGraphPass.id == id)
                return &renderGraphPass;

        return nullptr;
    }

    struct PassExecutionProfilingData
    {
        std::string passName;
        uint32_t drawCalls{0};
        double cpuTimeMs{0.0};
        uint32_t startQueryIndex{UINT32_MAX};
        uint32_t endQueryIndex{UINT32_MAX};
    };

    void initTimestampQueryPool();
    void destroyTimestampQueryPool();
    void resolveFrameProfilingData(uint32_t frameIndex);

    void createDescriptorSetPool();
    void createCameraDescriptorSets();
    void createPerObjectDescriptorSets();
    void createPreviewCameraDescriptorSets();

    static constexpr uint32_t MAX_RENDER_JOBS = 255;

    void sortRenderGraphPasses();
    void prepareFrameDataFromScene(Scene *scene);

    void recreateSwapChain();

    bool begin();
    void end();

    RGPResourcesBuilder m_renderGraphPassesBuilder;
    RGPResourcesCompiler m_renderGraphPassesCompiler;
    RGPResourcesStorage m_renderGraphPassesStorage;

    void compile();
    std::unordered_map<std::type_index, RenderGraphPassData> m_renderGraphPasses;
    std::vector<IRenderGraphPass *> m_sortedRenderGraphPasses;

    VkDevice m_device{VK_NULL_HANDLE};

    core::SwapChain::SharedPtr m_swapchain{nullptr};

    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};

    core::DescriptorPool::SharedPtr m_descriptorPool{VK_NULL_HANDLE};

    std::vector<core::Buffer::SharedPtr> m_lightSSBOs;

    std::vector<core::Buffer::SharedPtr> m_bonesSSBOs;
    std::vector<VkDescriptorSet> m_perObjectDescriptorSets;

    std::vector<void *> m_cameraMapped;
    std::vector<core::Buffer::SharedPtr> m_cameraUniformObjects;
    std::vector<VkDescriptorSet> m_cameraDescriptorSets;

    std::vector<void *> m_previewCameraMapped;
    std::vector<core::Buffer::SharedPtr> m_previewCameraUniformObjects;
    std::vector<VkDescriptorSet> m_previewCameraDescriptorSets;

    std::vector<void *> m_lightMapped;
    std::vector<core::Buffer::SharedPtr> m_lightSpaceMatrixUniformObjects;

    std::vector<core::CommandBuffer::SharedPtr> m_commandBuffers;
    std::vector<std::vector<core::CommandBuffer::SharedPtr>> m_secondaryCommandBuffers;
    std::vector<core::CommandPool::SharedPtr> m_commandPools;

    RenderGraphPassPerFrameData m_perFrameData;
    RenderGraphPassContext m_passContextData;

    std::vector<VkFence> m_inFlightFences;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;

    std::unordered_map<std::size_t, GPUMesh::SharedPtr> m_meshes;
    std::unordered_map<std::string, Material::SharedPtr> m_materialsByAlbedoPath;

    VkQueryPool m_timestampQueryPool{VK_NULL_HANDLE};
    uint32_t m_timestampQueryCapacity{0};
    uint32_t m_timestampQueriesPerFrame{0};
    uint32_t m_timestampQueryBase{0};
    uint32_t m_usedTimestampQueries{0};
    std::array<uint32_t, MAX_FRAMES_IN_FLIGHT> m_usedTimestampQueriesByFrame{};
    float m_timestampPeriodNs{0.0f};
    bool m_isGpuTimingAvailable{false};
    uint64_t m_profiledFrameIndex{0};

    std::array<std::vector<PassExecutionProfilingData>, MAX_FRAMES_IN_FLIGHT> m_passExecutionProfilingDataByFrame;
    std::array<bool, MAX_FRAMES_IN_FLIGHT> m_hasPendingProfilingResolve{};
    RenderGraphFrameProfilingData m_lastFrameProfilingData;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_HPP
