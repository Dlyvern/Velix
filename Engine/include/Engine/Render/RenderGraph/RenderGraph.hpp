#ifndef ELIX_RENDER_GRAPH_HPP
#define ELIX_RENDER_GRAPH_HPP

#include "Core/Macros.hpp"
#include "Core/SwapChain.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/CommandPool.hpp"
#include "Core/DescriptorPool.hpp"
#include "Core/Image.hpp"
#include "Core/Sampler.hpp"

#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Camera.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesBuilder.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesCompiler.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesStorage.hpp"
#include "Engine/Render/RenderGraph/PerFrameDataWorker.hpp"
#include "Engine/Render/RenderGraph/RenderGraphProfilingData.hpp"
#include "Engine/Render/RenderGraph/RenderGraphProfiling.hpp"
#include "Engine/RayTracing/RayTracingGeometryCache.hpp"
#include "Engine/RayTracing/RayTracingScene.hpp"
#include "Engine/RayTracing/SkinnedBlasBuilder.hpp"
#include "Engine/Render/MeshGeometryRegistry.hpp"
#include "Engine/Render/SceneMaterialResolver.hpp"
#include "Engine/Render/UnifiedGeometryBuffer.hpp"
#include "Engine/Render/GpuCullingSystem.hpp"
#include "Engine/Render/BindlessRegistry.hpp"

#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <string>
#include <cstdint>
#include <array>
#include <atomic>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

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
        bool enabled{true};             // false = skip draw + compile; VRAM freed via disablePass<T>()
    };

public:
    explicit RenderGraph(bool presentToSwapchain = true);
    ~RenderGraph();

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

    template <typename T>
    void connect(RGPOutputSlot<T> &from, RGPInputSlot<T> &to)
    {
        to.connectFrom(from);
        m_connections.push_back({from.getOwner(), to.getOwner()});
    }

    template <typename T>
    T *getPass()
    {
        const auto type = std::type_index(typeid(T));
        auto it = m_renderGraphPasses.find(type);
        return it == m_renderGraphPasses.end() ? nullptr : static_cast<T *>(it->second.renderGraphPass.get());
    }

    template <typename T>
    void disablePass()
    {
        const auto type = std::type_index(typeid(T));
        auto it = m_renderGraphPasses.find(type);
        if (it != m_renderGraphPasses.end())
            disablePassData(it->second);
    }

    template <typename T>
    void enablePass()
    {
        const auto type = std::type_index(typeid(T));
        auto it = m_renderGraphPasses.find(type);
        if (it != m_renderGraphPasses.end())
            enablePassData(it->second);
    }

    struct PassGroup
    {
        std::string name;
        std::vector<std::type_index> passes;
    };

    void registerGroup(PassGroup group);
    void disableGroup(const std::string &name);
    void enableGroup(const std::string &name);

    struct ProbeCaptureResult
    {
        std::shared_ptr<core::Image>   image;
        VkImageView                    cubeImageView{VK_NULL_HANDLE};
        std::shared_ptr<core::Sampler> sampler;
        bool success() const { return image && cubeImageView != VK_NULL_HANDLE && sampler; }
    };

    /// Renders all static draw batches from the probe world position to a cubemap.
    /// If scene is provided, rebuilds draw batches without frustum culling so all scene
    /// objects are captured regardless of editor camera position.
    /// Blocks the GPU (vkDeviceWaitIdle + vkQueueWaitIdle) during capture.
    ProbeCaptureResult captureSceneProbe(const glm::vec3 &probePos, uint32_t faceSize = 256, Scene *scene = nullptr);

    void prepareFrame(Camera::SharedPtr camera, Scene *scene, float deltaTime);

    void setCpuFrustumCullingEnabled(bool enabled)
    {
        m_enableCpuFrustumCulling = enabled;
    }

    void setCpuSmallFeatureCullingEnabled(bool enabled)
    {
        m_enableCpuSmallFeatureCulling = enabled;
    }

    void draw();
    void setup();

    const RenderGraphFrameProfilingData &getLastFrameProfilingData() const
    {
        return m_renderGraphProfiling->getLastFrameProfilingData();
    }

    RenderGraphFrameProfilingData getLastFrameBenchmarkData();
    std::vector<std::string> getActivePassDebugNames() const;

    uint32_t getCurrentImageIndex() const
    {
        return m_imageIndex;
    }

    std::vector<VkImageView> getImageViews(const std::vector<RGPResourceHandler> &handlers) const;

    static constexpr uint16_t MAX_FRAMES_IN_FLIGHT = 2;

    void createRenderGraphResources();

    void cleanResources();

private:
    struct RGPConnection
    {
        IRenderGraphPass *fromOwner{nullptr};
        IRenderGraphPass *toOwner{nullptr};
    };

    std::vector<RGPConnection> m_connections;

    RenderGraphPassData *findRenderGraphPassById(uint32_t id)
    {
        for (auto &[_, renderGraphPass] : m_renderGraphPasses)
            if (renderGraphPass.id == id)
                return &renderGraphPass;

        return nullptr;
    }

    std::unique_ptr<RenderGraphProfiling> m_renderGraphProfiling{nullptr};

    void createDescriptorSetPool();
    void createCameraDescriptorSets();
    void createPerObjectDescriptorSets();
    void createPreviewCameraDescriptorSets();
    void refreshCameraDescriptorSet(uint32_t frameIndex);

    static constexpr uint32_t MAX_RENDER_JOBS = 64;

    void sortRenderGraphPasses();
    std::unordered_map<RGPResourceHandler, RGPResourceHandler> buildAliasedTextureRoots();

    void disablePassData(RenderGraphPassData &data);
    void enablePassData(RenderGraphPassData &data);
    void prepareFrameDataFromScene(Scene *scene, const glm::mat4 &view, const glm::mat4 &projection, bool enableFrustumCulling);

    void recreateSwapChain();

    /// Scans all passes for needsRecompilation() and recompiles dirty ones.
    /// Returns true if any pass was recompiled.
    bool recompileDirtyPasses();

    bool begin();
    void end();

    RGPResourcesBuilder m_renderGraphPassesBuilder;
    RGPResourcesCompiler m_renderGraphPassesCompiler;
    RGPResourcesStorage m_renderGraphPassesStorage;

    void compile();
    std::unordered_map<std::type_index, RenderGraphPassData> m_renderGraphPasses;
    std::vector<IRenderGraphPass *> m_sortedRenderGraphPasses;
    std::vector<uint32_t> m_sortedRenderGraphPassIds;

    VkDevice m_device{VK_NULL_HANDLE};

    core::SwapChain::SharedPtr m_swapchain{nullptr};

    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};

    core::DescriptorPool::SharedPtr m_descriptorPool{VK_NULL_HANDLE};

    std::vector<core::Buffer::SharedPtr> m_lightSSBOs;

    std::vector<core::Buffer::SharedPtr> m_bonesSSBOs;
    std::vector<core::Buffer::SharedPtr> m_instanceSSBOs;
    std::vector<core::Buffer::SharedPtr> m_shadowInstanceSSBOs;
    std::vector<VkDescriptorSet> m_perObjectDescriptorSets;
    std::vector<VkDescriptorSet> m_shadowPerObjectDescriptorSets;

    std::vector<void *> m_cameraMapped;
    std::vector<core::Buffer::SharedPtr> m_cameraUniformObjects;
    std::vector<VkDescriptorSet> m_cameraDescriptorSets;

    std::vector<void *> m_previewCameraMapped;
    std::vector<core::Buffer::SharedPtr> m_previewCameraUniformObjects;
    std::vector<VkDescriptorSet> m_previewCameraDescriptorSets;

    std::vector<void *> m_lightMapped;
    std::vector<core::Buffer::SharedPtr> m_lightSpaceMatrixUniformObjects;

    std::vector<core::CommandBuffer::SharedPtr> m_commandBuffers;
    std::vector<std::vector<core::CommandPool::SharedPtr>> m_secondaryCommandPools;
    std::vector<std::vector<core::CommandBuffer::SharedPtr>> m_secondaryCommandBuffers;
    std::vector<core::CommandPool::SharedPtr> m_commandPools;

    RenderGraphPassPerFrameData m_perFrameData;
    RenderGraphPassContext m_passContextData;

    std::vector<VkFence> m_inFlightFences;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::array<std::vector<VkSemaphore>, MAX_FRAMES_IN_FLIGHT> m_uploadWaitSemaphoresByFrame;

    MeshGeometryRegistry m_meshGeometryRegistry;
    SceneMaterialResolver m_sceneMaterialResolver;
    rayTracing::RayTracingGeometryCache m_rayTracingGeometryCache;
    rayTracing::RayTracingScene m_rayTracingScene{MAX_FRAMES_IN_FLIGHT};
    rayTracing::SkinnedBlasBuilder m_skinnedBlasBuilder;

    // Unified geometry buffer for static meshes – eliminates per-draw VB/IB rebinds.
    UnifiedGeometryBuffer m_staticUnifiedGeometry;
    static constexpr VkDeviceSize UNIFIED_VERTEX_BUFFER_SIZE = 512ULL * 1024 * 1024; // 512 MB
    static constexpr uint32_t UNIFIED_INDEX_BUFFER_COUNT = 64 * 1024 * 1024;         // 64 M indices

    GpuCullingSystem m_gpuCulling;

    std::array<glm::vec4, 6> m_lastFrustumPlanes{}; // stored in prepareFrame, used in begin()
    bool m_lastFrustumCullingEnabled{false};
    bool m_enableCpuFrustumCulling{true};
    bool m_enableCpuSmallFeatureCulling{true};

    bool m_presentToSwapchain{true};

    BindlessRegistry m_bindlessRegistry;

    // Per-pass execution and barrier cache to avoid per-frame heap allocations.
    // Keyed by pass index in m_sortedRenderGraphPasses. Invalidated on recompile.
    struct CachedPassExecutionData
    {
        uint32_t imageIndex{UINT32_MAX};
        uint32_t directionalShadowCount{UINT32_MAX};
        uint32_t spotShadowCount{UINT32_MAX};
        uint32_t pointShadowCount{UINT32_MAX};
        uint64_t executionCacheKey{UINT64_MAX};
        bool valid{false};
        std::vector<IRenderGraphPass::RenderPassExecution> executions;
        std::vector<std::vector<VkImageMemoryBarrier2>> preBarriers;  // [executionIdx]
        std::vector<std::vector<VkImageMemoryBarrier2>> postBarriers; // [executionIdx]
    };

    std::vector<CachedPassExecutionData> m_passExecutionCache;
    void invalidateAllExecutionCaches();
    void buildExecutionCacheForPass(size_t sortedPassIndex);

    std::unordered_map<std::string, PassGroup> m_passGroups;

    std::atomic<bool> m_swapchainResizeRequested{false};
    bool m_hasWindowResizeCallback{false};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_HPP
