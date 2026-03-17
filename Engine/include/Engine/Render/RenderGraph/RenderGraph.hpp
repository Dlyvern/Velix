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
#include "Engine/RayTracing/RayTracingGeometryCache.hpp"
#include "Engine/RayTracing/RayTracingScene.hpp"
#include "Engine/Render/UnifiedGeometryBuffer.hpp"

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

    static constexpr uint16_t MAX_FRAMES_IN_FLIGHT = 1;

    VkDescriptorPool getDescriptorPool() const
    {
        return m_descriptorPool;
    }

    void createRenderGraphResources();

    rayTracing::RayTracingGeometryCache &getRayTracingGeometryCache()
    {
        return m_rayTracingGeometryCache;
    }

    const rayTracing::RayTracingGeometryCache &getRayTracingGeometryCache() const
    {
        return m_rayTracingGeometryCache;
    }

    rayTracing::RayTracingScene &getRayTracingScene()
    {
        return m_rayTracingScene;
    }

    const rayTracing::RayTracingScene &getRayTracingScene() const
    {
        return m_rayTracingScene;
    }

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
    void refreshCameraDescriptorSet(uint32_t frameIndex);

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
    rayTracing::RayTracingGeometryCache m_rayTracingGeometryCache;
    rayTracing::RayTracingScene m_rayTracingScene{MAX_FRAMES_IN_FLIGHT};

    // Unified geometry buffer for static meshes – eliminates per-draw VB/IB rebinds.
    UnifiedGeometryBuffer m_staticUnifiedGeometry;
    static constexpr VkDeviceSize  UNIFIED_VERTEX_BUFFER_SIZE = 512ULL * 1024 * 1024; // 512 MB
    static constexpr uint32_t      UNIFIED_INDEX_BUFFER_COUNT = 64 * 1024 * 1024;     // 64 M indices

    // ---- GPU frustum-culling compute pass ----
    // Push constants layout (112 bytes, fits the 128-byte Vulkan minimum guarantee).
    struct GpuCullPushConstants
    {
        uint32_t  batchCount;
        uint32_t  pad[3];        // pad to align the vec4 array
        glm::vec4 planes[6];     // normalised frustum planes
    };
    static_assert(sizeof(GpuCullPushConstants) <= 128, "GpuCullPushConstants exceeds push constant limit");

    static constexpr uint32_t MAX_GPU_CULL_BATCHES = 30000u;

    std::vector<core::Buffer::SharedPtr> m_batchBoundsSSBOs;    // vec4[] per batch
    std::vector<core::Buffer::SharedPtr> m_indirectDrawBuffers; // VkDrawIndexedIndirectCommand[]
    std::vector<VkDescriptorSet>         m_gpuCullDescriptorSets;

    VkDescriptorPool      m_gpuCullDescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_gpuCullDescriptorSetLayout{VK_NULL_HANDLE};
    VkPipelineLayout      m_gpuCullPipelineLayout{VK_NULL_HANDLE};
    VkPipeline            m_gpuCullPipeline{VK_NULL_HANDLE};

    std::array<glm::vec4, 6> m_lastFrustumPlanes{};  // stored in prepareFrame, used in begin()
    bool                     m_lastFrustumCullingEnabled{false};

    void createGpuCullingPipeline();
    void dispatchGpuCulling(VkCommandBuffer cmd);
    std::unordered_map<std::size_t, MeshLocalBounds> m_meshLocalBoundsByHash;
    std::unordered_map<std::string, Texture::SharedPtr> m_texturesByResolvedPath;
    std::unordered_set<std::string> m_failedTextureResolvedPaths;
    std::unordered_map<std::string, Material::SharedPtr> m_materialsByAlbedoPath;
    std::unordered_set<std::string> m_failedAlbedoTexturePaths;
    std::unordered_map<std::string, Material::SharedPtr> m_materialsByAssetPath;
    std::unordered_set<std::string> m_failedMaterialAssetPaths;

    bool m_presentToSwapchain{true};

    // ---- Bindless material system ----
    // One global descriptor set holds all material textures (binding 0, array indexed by uint)
    // and a SSBO of GPUParams (binding 1, indexed by materialIndex).
    // GBuffer passes bind this set once per frame at Set 1, eliminating all per-batch material binds.
    VkDescriptorPool  m_bindlessPool{VK_NULL_HANDLE};
    VkDescriptorSet   m_bindlessSet{VK_NULL_HANDLE};

    core::Buffer::SharedPtr              m_materialParamsSSBO;
    std::vector<Material::GPUParams>     m_cpuMaterialParams; // CPU mirror, indexed by materialIndex

    std::unordered_map<Texture  *, uint32_t> m_textureRegistry;   // texture ptr → slot in allTextures[]
    std::unordered_map<Material *, uint32_t> m_materialRegistry;  // material ptr → materialIndex

    uint32_t m_nextTextureSlot{0};
    uint32_t m_nextMaterialSlot{0};

    void createBindlessResources();
    uint32_t getOrRegisterTexture(Texture *tex);
    uint32_t getOrRegisterMaterial(Material *mat);

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

    std::atomic<bool> m_swapchainResizeRequested{false};
    bool m_hasWindowResizeCallback{false};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RENDER_GRAPH_HPP
