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
#include "Engine/Render/RenderGraph/RenderGraphProfiling.hpp"

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
    explicit RenderGraph(bool presentToSwapchain = true);

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
        return m_renderGraphProfiling->getLastFrameProfilingData();
    }

    uint32_t getCurrentImageIndex() const
    {
        return m_imageIndex;
    }

    static constexpr uint16_t MAX_FRAMES_IN_FLIGHT = 2;

    VkDescriptorPool getDescriptorPool() const
    {
        return m_descriptorPool;
    }

    void createRenderGraphResources();

    void cleanResources();

private:
    struct MeshLocalBounds
    {
        glm::vec3 center{0.0f};
        float radius{0.0f};
    };

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

    static constexpr uint32_t MAX_RENDER_JOBS = 64;

    void sortRenderGraphPasses();
    void prepareFrameDataFromScene(Scene *scene, const glm::mat4 &view, const glm::mat4 &projection, bool enableFrustumCulling);

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

    std::unordered_map<std::size_t, GPUMesh::SharedPtr> m_meshes;
    std::unordered_map<std::size_t, MeshLocalBounds> m_meshLocalBoundsByHash;
    std::unordered_map<std::string, Texture::SharedPtr> m_texturesByResolvedPath;
    std::unordered_set<std::string> m_failedTextureResolvedPaths;
    std::unordered_map<std::string, Material::SharedPtr> m_materialsByAlbedoPath;
    std::unordered_set<std::string> m_failedAlbedoTexturePaths;
    std::unordered_map<std::string, Material::SharedPtr> m_materialsByAssetPath;
    std::unordered_set<std::string> m_failedMaterialAssetPaths;

    bool m_presentToSwapchain{true};

    // Per-pass execution and barrier cache to avoid per-frame heap allocations.
    // Keyed by pass index in m_sortedRenderGraphPasses. Invalidated on recompile.
    struct CachedPassExecutionData
    {
        uint32_t imageIndex{UINT32_MAX};
        uint32_t directionalShadowCount{UINT32_MAX};
        uint32_t spotShadowCount{UINT32_MAX};
        uint32_t pointShadowCount{UINT32_MAX};
        bool valid{false};
        std::vector<IRenderGraphPass::RenderPassExecution> executions;
        std::vector<std::vector<VkImageMemoryBarrier2>> preBarriers;  // [executionIdx]
        std::vector<std::vector<VkImageMemoryBarrier2>> postBarriers; // [executionIdx]
    };
    std::vector<CachedPassExecutionData> m_passExecutionCache;
    void invalidateAllExecutionCaches();
    void buildExecutionCacheForPass(size_t sortedPassIndex);

    std::atomic<bool> m_swapchainResizeRequested{false};
    bool m_hasWindowResizeCallback{false};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_HPP
