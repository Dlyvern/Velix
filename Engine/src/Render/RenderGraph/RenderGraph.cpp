#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/ShaderHandler.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"
#include "Engine/Render/ObjectIdEncoding.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/GraphPasses/ShadowRenderGraphPass.hpp"
#include "Engine/Caches/Hash.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"

#include <iostream>
#include <array>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_set>
#include <limits>
#include <cmath>
#include <cfloat>
#include <exception>
#include <optional>
#include <filesystem>
#include <sstream>
#include <fstream>

#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/TerrainComponent.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Builders/GraphicsPipelineManager.hpp"
#include "Engine/Assets/AssetsLoader.hpp"

#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Utilities/ImageUtilities.hpp"
#include "Engine/Shaders/ShaderCompiler.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Threads/ThreadPoolManager.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct CameraUBO
{
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 invView;
    glm::mat4 invProjection;
};

struct LightSSBO
{
    int lightCount;
    glm::vec3 padding{0.0f};
    elix::engine::RenderGraphLightData lights[];
};

namespace
{
    struct TextureAliasSignature
    {
        VkExtent2D extent{0u, 0u};
        elix::engine::renderGraph::RGPTextureUsage usage{elix::engine::renderGraph::RGPTextureUsage::SAMPLED};
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkImageLayout initialLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkImageLayout finalLayout{VK_IMAGE_LAYOUT_UNDEFINED};
        VkSampleCountFlagBits sampleCount{VK_SAMPLE_COUNT_1_BIT};
        uint32_t arrayLayers{1u};
        VkImageCreateFlags flags{0u};
        VkImageViewType viewType{VK_IMAGE_VIEW_TYPE_2D};

        bool operator==(const TextureAliasSignature &other) const
        {
            return extent.width == other.extent.width &&
                   extent.height == other.extent.height &&
                   usage == other.usage &&
                   format == other.format &&
                   initialLayout == other.initialLayout &&
                   finalLayout == other.finalLayout &&
                   sampleCount == other.sampleCount &&
                   arrayLayers == other.arrayLayers &&
                   flags == other.flags &&
                   viewType == other.viewType;
        }
    };

    struct TextureAliasSignatureHasher
    {
        size_t operator()(const TextureAliasSignature &signature) const noexcept
        {
            size_t seed = 0u;
            elix::engine::hashing::hashCombine(seed, signature.extent.width);
            elix::engine::hashing::hashCombine(seed, signature.extent.height);
            elix::engine::hashing::hashCombine(seed, static_cast<uint32_t>(signature.usage));
            elix::engine::hashing::hashCombine(seed, static_cast<uint32_t>(signature.format));
            elix::engine::hashing::hashCombine(seed, static_cast<uint32_t>(signature.initialLayout));
            elix::engine::hashing::hashCombine(seed, static_cast<uint32_t>(signature.finalLayout));
            elix::engine::hashing::hashCombine(seed, static_cast<uint32_t>(signature.sampleCount));
            elix::engine::hashing::hashCombine(seed, signature.arrayLayers);
            elix::engine::hashing::hashCombine(seed, signature.flags);
            elix::engine::hashing::hashCombine(seed, static_cast<uint32_t>(signature.viewType));
            return seed;
        }
    };

    uint32_t sampleCountMultiplier(VkSampleCountFlagBits sampleCount)
    {
        switch (sampleCount)
        {
        case VK_SAMPLE_COUNT_1_BIT:
            return 1u;
        case VK_SAMPLE_COUNT_2_BIT:
            return 2u;
        case VK_SAMPLE_COUNT_4_BIT:
            return 4u;
        case VK_SAMPLE_COUNT_8_BIT:
            return 8u;
        case VK_SAMPLE_COUNT_16_BIT:
            return 16u;
        case VK_SAMPLE_COUNT_32_BIT:
            return 32u;
        case VK_SAMPLE_COUNT_64_BIT:
            return 64u;
        default:
            return 1u;
        }
    }

    uint64_t bytesPerTexel(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8_UNORM:
            return 1ull;

        case VK_FORMAT_D16_UNORM:
            return 2ull;

        case VK_FORMAT_R32_UINT:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return 4ull;

        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8ull;

        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16ull;

        default:
            return 4ull;
        }
    }

    uint64_t estimateTextureDescriptionVramBytes(const elix::engine::renderGraph::RGPTextureDescription &description)
    {
        if (description.getIsSwapChainTarget())
            return 0ull;

        const VkExtent2D extent = description.getCustomExtentFunction()
                                      ? description.getCustomExtentFunction()()
                                      : description.getExtent();
        if (extent.width == 0u || extent.height == 0u)
            return 0ull;

        const uint64_t pixelCount = static_cast<uint64_t>(extent.width) *
                                    static_cast<uint64_t>(extent.height) *
                                    static_cast<uint64_t>(std::max(description.getArrayLayers(), 1u)) *
                                    static_cast<uint64_t>(sampleCountMultiplier(description.getSampleCount()));

        return pixelCount * bytesPerTexel(description.getFormat());
    }

    const elix::engine::RenderTarget *findTargetByImageView(
        const std::unordered_map<elix::engine::renderGraph::RGPResourceHandler, const elix::engine::RenderTarget *> &targets,
        VkImageView imageView)
    {
        if (imageView == VK_NULL_HANDLE)
            return nullptr;

        for (const auto &[_, target] : targets)
        {
            if (target && target->vkImageView() == imageView)
                return target;
        }

        return nullptr;
    }

    std::optional<VkExtent2D> getExecutionMinAttachmentExtent(const elix::engine::renderGraph::IRenderGraphPass::RenderPassExecution &execution)
    {
        uint32_t minWidth = std::numeric_limits<uint32_t>::max();
        uint32_t minHeight = std::numeric_limits<uint32_t>::max();
        bool hasAttachmentExtent = false;

        const auto accumulateExtent = [&](const elix::engine::RenderTarget *target)
        {
            if (!target)
                return;

            const VkExtent2D extent = target->getExtent();
            minWidth = std::min(minWidth, extent.width);
            minHeight = std::min(minHeight, extent.height);
            hasAttachmentExtent = true;
        };

        for (const auto &color : execution.colorsRenderingItems)
            accumulateExtent(findTargetByImageView(execution.targets, color.imageView));

        if (execution.useDepth)
            accumulateExtent(findTargetByImageView(execution.targets, execution.depthRenderingItem.imageView));

        // Fall back to any declared target if a pass forgot to map its attachment
        // view back into execution.targets.
        if (!hasAttachmentExtent)
        {
            for (const auto &[_, target] : execution.targets)
                accumulateExtent(target);
        }

        if (!hasAttachmentExtent)
            return std::nullopt;

        return VkExtent2D{minWidth, minHeight};
    }

    void sanitizeExecutionRenderArea(VkRect2D &renderArea, const std::optional<VkExtent2D> &attachmentExtent)
    {
        renderArea.offset.x = std::max(renderArea.offset.x, 0);
        renderArea.offset.y = std::max(renderArea.offset.y, 0);

        if (!attachmentExtent.has_value())
        {
            renderArea.extent.width = std::max(renderArea.extent.width, 1u);
            renderArea.extent.height = std::max(renderArea.extent.height, 1u);
            return;
        }

        const uint32_t width = std::max(1u, attachmentExtent->width);
        const uint32_t height = std::max(1u, attachmentExtent->height);

        const int32_t maxOffsetX = static_cast<int32_t>(width - 1u);
        const int32_t maxOffsetY = static_cast<int32_t>(height - 1u);
        renderArea.offset.x = std::clamp(renderArea.offset.x, 0, maxOffsetX);
        renderArea.offset.y = std::clamp(renderArea.offset.y, 0, maxOffsetY);

        const uint32_t maxWidth = std::max(1u, width - static_cast<uint32_t>(renderArea.offset.x));
        const uint32_t maxHeight = std::max(1u, height - static_cast<uint32_t>(renderArea.offset.y));
        const uint32_t requestedWidth = renderArea.extent.width == 0u ? maxWidth : renderArea.extent.width;
        const uint32_t requestedHeight = renderArea.extent.height == 0u ? maxHeight : renderArea.extent.height;
        renderArea.extent.width = std::clamp(requestedWidth, 1u, maxWidth);
        renderArea.extent.height = std::clamp(requestedHeight, 1u, maxHeight);
    }

    void normalizeExecutionAttachments(elix::engine::renderGraph::IRenderGraphPass::RenderPassExecution &execution)
    {
        execution.colorFormats.resize(execution.colorsRenderingItems.size(), VK_FORMAT_UNDEFINED);

        for (size_t colorIndex = 0; colorIndex < execution.colorsRenderingItems.size(); ++colorIndex)
        {
            const auto *target = findTargetByImageView(execution.targets, execution.colorsRenderingItems[colorIndex].imageView);
            if (!target)
                continue;

            execution.colorFormats[colorIndex] = target->getFormat();
        }

        if (execution.useDepth)
        {
            const auto *depthTarget = findTargetByImageView(execution.targets, execution.depthRenderingItem.imageView);
            if (depthTarget)
                execution.depthFormat = depthTarget->getFormat();
        }

        sanitizeExecutionRenderArea(execution.renderArea, getExecutionMinAttachmentExtent(execution));
    }

}

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

using namespace elix::engine::hashing;

RenderGraph::~RenderGraph()
{
    if (m_device != VK_NULL_HANDLE &&
        m_renderGraphProfiling != nullptr &&
        core::VulkanContext::getContext() != nullptr)
        cleanResources();
}

RenderGraph::RenderGraph(bool presentToSwapchain) : m_presentToSwapchain(presentToSwapchain)
{
    m_device = core::VulkanContext::getContext()->getDevice();
    m_swapchain = core::VulkanContext::getContext()->getSwapchain();
    m_meshGeometryRegistry.setUnifiedGeometryBuffer(&m_staticUnifiedGeometry, UNIFIED_VERTEX_BUFFER_SIZE, UNIFIED_INDEX_BUFFER_COUNT);

    m_commandBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
    m_secondaryCommandPools.resize(MAX_FRAMES_IN_FLIGHT);
    m_secondaryCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_commandPools.reserve(MAX_FRAMES_IN_FLIGHT);

    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled
    fenceInfo.pNext = nullptr;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        auto result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);

        if (result != VK_SUCCESS)
            throw std::runtime_error("Failed to create semaphore: " + core::helpers::vulkanResultToString(result));

        result = vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]);

        if (result != VK_SUCCESS)
            throw std::runtime_error("Failed to create fence: " + core::helpers::vulkanResultToString(result));
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create renderFinished semaphore for image!");
}

RenderGraphFrameProfilingData RenderGraph::getLastFrameBenchmarkData()
{
    RenderGraphFrameProfilingData benchmarkData = m_renderGraphProfiling->getLastFrameProfilingData();
    const auto aliasRoots = buildAliasedTextureRoots();

    std::unordered_map<std::string, uint64_t> passVramBytesByName;
    passVramBytesByName.reserve(m_sortedRenderGraphPassIds.size());
    std::unordered_set<RGPResourceHandler> uniqueGraphRoots;
    uniqueGraphRoots.reserve(m_renderGraphPasses.size() * 2u);

    for (const uint32_t passId : m_sortedRenderGraphPassIds)
    {
        auto *passData = findRenderGraphPassById(passId);
        if (!passData || !passData->enabled || !passData->renderGraphPass)
            continue;

        std::unordered_set<RGPResourceHandler> passRoots;
        passRoots.reserve(passData->passInfo.writes.size());
        uint64_t passVramBytes = 0ull;

        for (const auto &writeAccess : passData->passInfo.writes)
        {
            const auto *textureDescription = m_renderGraphPassesBuilder.getTextureDescription(writeAccess.resourceId);
            if (!textureDescription || textureDescription->getIsSwapChainTarget())
                continue;

            const auto aliasRootIt = aliasRoots.find(writeAccess.resourceId);
            const RGPResourceHandler rootHandler = aliasRootIt != aliasRoots.end() ? aliasRootIt->second : writeAccess.resourceId;
            if (!passRoots.insert(rootHandler).second)
                continue;

            const uint64_t textureVramBytes = estimateTextureDescriptionVramBytes(*textureDescription);
            passVramBytes += textureVramBytes;

            if (uniqueGraphRoots.insert(rootHandler).second)
                benchmarkData.renderGraphVramBytes += textureVramBytes;
        }

        passVramBytesByName[passData->renderGraphPass->getDebugName()] += passVramBytes;
    }

    for (auto &passProfilingData : benchmarkData.passes)
    {
        if (const auto it = passVramBytesByName.find(passProfilingData.passName); it != passVramBytesByName.end())
            passProfilingData.vramBytes = it->second;
    }

    return benchmarkData;
}

std::vector<VkImageView> RenderGraph::getImageViews(const std::vector<RGPResourceHandler> &handlers) const
{
    std::vector<VkImageView> imageViews;
    imageViews.reserve(handlers.size());

    for (const auto &handler : handlers)
    {
        const auto *renderTarget = m_renderGraphPassesStorage.getTexture(handler);
        imageViews.push_back(renderTarget ? renderTarget->vkImageView() : VK_NULL_HANDLE);
    }

    return imageViews;
}

void RenderGraph::prepareFrameDataFromScene(Scene *scene, const glm::mat4 &view, const glm::mat4 &projection, bool enableFrustumCulling)
{
    m_sceneMaterialResolver.beginFrame();

    auto perFrameWorker = PerFrameDataWorker::begin(
        m_perFrameData,
        PerFrameDataWorker::Dependencies{
            .meshGeometryRegistry = &m_meshGeometryRegistry,
            .materialResolver = &m_sceneMaterialResolver,
            .bindlessRegistry = &m_bindlessRegistry,
            .rayTracingScene = &m_rayTracingScene,
            .rayTracingGeometryCache = &m_rayTracingGeometryCache,
            .skinnedBlasBuilder = &m_skinnedBlasBuilder,
            .lastFrustumPlanes = &m_lastFrustumPlanes,
            .lastFrustumCullingEnabled = &m_lastFrustumCullingEnabled,
            .currentFrame = m_currentFrame,
            .enableCpuSmallFeatureCulling = m_enableCpuSmallFeatureCulling});

    const glm::vec3 cameraWorldPos = glm::vec3(glm::inverse(view)[3]);

    perFrameWorker.pruneRemovedEntities(scene);
    perFrameWorker.syncSceneDrawItems(scene, cameraWorldPos);

    utilities::AsyncGpuUpload::batchFlush(core::VulkanContext::getContext()->getGraphicsQueue());

    perFrameWorker.buildFrameBones();
    perFrameWorker.buildDrawReferences(view, projection, enableFrustumCulling);
    perFrameWorker.sortDrawReferences(cameraWorldPos);
    perFrameWorker.buildRayTracingInputs();
    perFrameWorker.buildRasterBatches();
    perFrameWorker.buildShadowBatches();

    const auto &frameBones = perFrameWorker.getFrameBones();
    const auto &shadowPerObjectInstances = perFrameWorker.getShadowPerObjectInstances();

    const VkDeviceSize requiredBonesSize = static_cast<VkDeviceSize>(frameBones.size() * sizeof(glm::mat4));
    const VkDeviceSize availableBonesSize = m_bonesSSBOs[m_currentFrame]->getSize();
    if (requiredBonesSize > availableBonesSize)
        throw std::runtime_error("Bones SSBO size is too small for current frame. Increase initial bones buffer size.");

    const VkDeviceSize requiredInstanceSize = static_cast<VkDeviceSize>(m_perFrameData.perObjectInstances.size() * sizeof(PerObjectInstanceData));
    const VkDeviceSize availableInstanceSize = m_instanceSSBOs[m_currentFrame]->getSize();
    if (requiredInstanceSize > availableInstanceSize)
        throw std::runtime_error("Instance SSBO size is too small for current frame. Increase initial instance buffer size.");

    const VkDeviceSize requiredShadowInstanceSize = static_cast<VkDeviceSize>(shadowPerObjectInstances.size() * sizeof(PerObjectInstanceData));
    const VkDeviceSize availableShadowInstanceSize = m_shadowInstanceSSBOs[m_currentFrame]->getSize();
    if (requiredShadowInstanceSize > availableShadowInstanceSize)
        throw std::runtime_error("Shadow instance SSBO size is too small for current frame. Increase initial shadow instance buffer size.");

    glm::mat4 *bonesMapped = nullptr;
    m_bonesSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(bonesMapped));
    if (!frameBones.empty())
        std::memcpy(bonesMapped, frameBones.data(), frameBones.size() * sizeof(glm::mat4));
    m_bonesSSBOs[m_currentFrame]->unmap();

    PerObjectInstanceData *instancesMapped = nullptr;
    m_instanceSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(instancesMapped));
    if (!m_perFrameData.perObjectInstances.empty())
        std::memcpy(instancesMapped, m_perFrameData.perObjectInstances.data(), requiredInstanceSize);
    m_instanceSSBOs[m_currentFrame]->unmap();

    PerObjectInstanceData *shadowInstancesMapped = nullptr;
    m_shadowInstanceSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(shadowInstancesMapped));
    if (!shadowPerObjectInstances.empty())
        std::memcpy(shadowInstancesMapped, shadowPerObjectInstances.data(), requiredShadowInstanceSize);
    m_shadowInstanceSSBOs[m_currentFrame]->unmap();

    m_perFrameData.perObjectDescriptorSet = m_perObjectDescriptorSets[m_currentFrame];
    m_perFrameData.shadowPerObjectDescriptorSet = m_shadowPerObjectDescriptorSets[m_currentFrame];

    if (m_gpuCulling.isInitialized())
    {
        const uint32_t batchCount = static_cast<uint32_t>(m_perFrameData.drawBatches.size());

        // Upload batch bounding spheres.
        if (batchCount > 0)
        {
            glm::vec4 *boundsMapped = nullptr;
            m_gpuCulling.getBatchBoundsSSBO(m_currentFrame)->map(reinterpret_cast<void *&>(boundsMapped));
            for (uint32_t i = 0; i < batchCount; ++i)
            {
                const GPUBatchBounds &b = m_perFrameData.batchBounds[i];
                boundsMapped[i] = glm::vec4(b.center, b.radius);
            }
            m_gpuCulling.getBatchBoundsSSBO(m_currentFrame)->unmap();
        }

        // Write VkDrawIndexedIndirectCommand[] from current draw batches.
        // The compute shader may zero out instanceCount for culled batches.
        {
            uint8_t *cmdMapped = nullptr;
            m_gpuCulling.getIndirectDrawBuffer(m_currentFrame)->map(reinterpret_cast<void *&>(cmdMapped));
            for (uint32_t i = 0; i < batchCount; ++i)
            {
                const DrawBatch &batch = m_perFrameData.drawBatches[i];
                VkDrawIndexedIndirectCommand cmd{};
                cmd.indexCount = batch.mesh ? batch.mesh->indicesCount : 0u;
                cmd.instanceCount = batch.instanceCount;
                cmd.firstIndex = (batch.mesh && batch.mesh->inUnifiedBuffer) ? batch.mesh->unifiedFirstIndex : 0u;
                cmd.vertexOffset = (batch.mesh && batch.mesh->inUnifiedBuffer) ? batch.mesh->unifiedVertexOffset : 0;
                cmd.firstInstance = 0u; // baseInstance comes from push constant
                std::memcpy(cmdMapped + i * sizeof(VkDrawIndexedIndirectCommand), &cmd, sizeof(cmd));
            }
            m_gpuCulling.getIndirectDrawBuffer(m_currentFrame)->unmap();
        }

        m_perFrameData.indirectDrawBuffer = m_gpuCulling.getIndirectDrawBuffer(m_currentFrame)->vk();
    }
}

RenderGraph::ProbeCaptureResult RenderGraph::captureSceneProbe(const glm::vec3 &probePos, uint32_t faceSize, Scene *scene)
{
    if (m_currentFrame >= m_cameraDescriptorSets.size() || m_currentFrame >= m_cameraMapped.size())
        return {};

    if (scene)
    {
        prepareFrameDataFromScene(scene, glm::mat4(1.0f), glm::mat4(1.0f), false);

        // Re-upload per-object instance data so the capture uses the new (complete) batches.
        const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(
            m_perFrameData.perObjectInstances.size() * sizeof(PerObjectInstanceData));
        if (requiredSize > 0)
        {
            const VkDeviceSize available = m_instanceSSBOs[m_currentFrame]->getSize();
            if (requiredSize <= available)
            {
                PerObjectInstanceData *mapped = nullptr;
                m_instanceSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(mapped));
                std::memcpy(mapped, m_perFrameData.perObjectInstances.data(), requiredSize);
                m_instanceSSBOs[m_currentFrame]->unmap();
            }
        }

        m_perFrameData.perObjectDescriptorSet = m_perObjectDescriptorSets[m_currentFrame];
    }

    if (m_perFrameData.drawBatches.empty() ||
        m_perFrameData.bindlessDescriptorSet == VK_NULL_HANDLE ||
        m_perFrameData.perObjectDescriptorSet == VK_NULL_HANDLE)
        return {};

    vkDeviceWaitIdle(m_device);

    // Six cube-face look directions and corresponding up vectors
    static const glm::vec3 kFaceDirs[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const glm::vec3 kFaceUps[6] = {
        {0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};

    const VkFormat kColorFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
    const VkFormat kDepthFmt = VK_FORMAT_D32_SFLOAT;

    auto captureImage = core::Image::createShared(
        VkExtent2D{faceSize, faceSize},
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        core::memory::MemoryUsage::GPU_ONLY,
        kColorFmt, VK_IMAGE_TILING_OPTIMAL,
        6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    auto depthImage = core::Image::createShared(
        VkExtent2D{faceSize, faceSize},
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        core::memory::MemoryUsage::GPU_ONLY,
        kDepthFmt);

    std::array<VkImageView, 6> faceViews{};
    for (uint32_t i = 0; i < 6; ++i)
    {
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ci.image = captureImage->vk();
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = kColorFmt;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, i, 1};
        vkCreateImageView(m_device, &ci, nullptr, &faceViews[i]);
    }

    VkImageView depthView = VK_NULL_HANDLE;
    {
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ci.image = depthImage->vk();
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = kDepthFmt;
        ci.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCreateImageView(m_device, &ci, nullptr, &depthView);
    }

    GraphicsPipelineKey key{};
    key.shader = ShaderId::ProbeCapture;
    key.blend = BlendMode::None;
    key.cull = CullMode::Back;
    key.depthTest = true;
    key.depthWrite = true;
    key.depthCompare = VK_COMPARE_OP_LESS;
    key.polygonMode = VK_POLYGON_MODE_FILL;
    key.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    key.pipelineLayout = EngineShaderFamilies::bindlessMeshPipelineLayout;
    key.colorFormats = {kColorFmt};
    key.depthFormat = kDepthFmt;
    const VkPipeline pipeline = GraphicsPipelineManager::getOrCreate(key);

    auto cleanup = [&]()
    {
        for (auto v : faceViews)
            if (v)
                vkDestroyImageView(m_device, v, nullptr);
        if (depthView)
            vkDestroyImageView(m_device, depthView, nullptr);
    };

    if (!pipeline)
    {
        cleanup();
        return {};
    }

    auto ctx = core::VulkanContext::getContext();
    auto cmdPool = ctx->getGraphicsCommandPool();
    auto cmdBuf = core::CommandBuffer::createShared(*cmdPool);
    cmdBuf->begin();

    {
        auto barrier = utilities::ImageUtilities::insertImageMemoryBarrier(
            *captureImage,
            0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6});
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmdBuf, &dep);
    }

    {
        auto barrier = utilities::ImageUtilities::insertImageMemoryBarrier(
            *depthImage,
            0, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1});
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmdBuf, &dep);
    }

    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 500.0f);
    proj[1][1] *= -1.0f;

    const VkViewport vp{0.0f, 0.0f, static_cast<float>(faceSize), static_cast<float>(faceSize), 0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {faceSize, faceSize}};
    const VkPipelineLayout pipelineLayout = EngineShaderFamilies::bindlessMeshPipelineLayout;

    struct CapturePushConstant
    {
        uint32_t baseInstance{0};
        uint32_t padding[3]{0, 0, 0};
        float time{0.0f};
    };

    for (uint32_t face = 0; face < 6; ++face)
    {
        const glm::mat4 view = glm::lookAt(probePos, probePos + kFaceDirs[face], kFaceUps[face]);
        CameraUBO ubo{view, proj, glm::inverse(view), glm::inverse(proj)};
        std::memcpy(m_cameraMapped[m_currentFrame], &ubo, sizeof(CameraUBO));

        VkRenderingAttachmentInfo colorAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colorAtt.imageView = faceViews[face];
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderingAttachmentInfo depthAtt{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAtt.imageView = depthView;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.clearValue.depthStencil = {1.0f, 0};

        VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderingInfo.renderArea = {{0, 0}, {faceSize, faceSize}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAtt;
        renderingInfo.pDepthAttachment = &depthAtt;

        vkCmdBeginRendering(cmdBuf, &renderingInfo);
        vkCmdSetViewport(cmdBuf, 0, 1, &vp);
        vkCmdSetScissor(cmdBuf, 0, 1, &sc);

        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        const VkDescriptorSet camDS = m_cameraDescriptorSets[m_currentFrame];
        const VkDescriptorSet bindless = m_perFrameData.bindlessDescriptorSet;
        const VkDescriptorSet perObj = m_perFrameData.perObjectDescriptorSet;
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &camDS, 0, nullptr);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1, &bindless, 0, nullptr);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1, &perObj, 0, nullptr);

        VkBuffer boundUnifiedVB = VK_NULL_HANDLE;
        VkBuffer boundUnifiedIB = VK_NULL_HANDLE;
        VkBuffer boundVB = VK_NULL_HANDLE;
        VkBuffer boundIB = VK_NULL_HANDLE;

        const bool hasUnified = m_perFrameData.unifiedStaticVertexBuffer != VK_NULL_HANDLE &&
                                m_perFrameData.unifiedStaticIndexBuffer != VK_NULL_HANDLE;

        for (const auto &batch : m_perFrameData.drawBatches)
        {
            if (!batch.mesh || !batch.material || batch.skinned || batch.instanceCount == 0)
                continue;

            const bool useUnified = hasUnified && batch.mesh->inUnifiedBuffer;

            if (useUnified)
            {
                if (m_perFrameData.unifiedStaticVertexBuffer != boundUnifiedVB)
                {
                    const VkDeviceSize off = 0;
                    vkCmdBindVertexBuffers(cmdBuf, 0, 1, &m_perFrameData.unifiedStaticVertexBuffer, &off);
                    boundUnifiedVB = m_perFrameData.unifiedStaticVertexBuffer;
                    boundVB = VK_NULL_HANDLE;
                }
                if (m_perFrameData.unifiedStaticIndexBuffer != boundUnifiedIB)
                {
                    vkCmdBindIndexBuffer(cmdBuf, m_perFrameData.unifiedStaticIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    boundUnifiedIB = m_perFrameData.unifiedStaticIndexBuffer;
                    boundIB = VK_NULL_HANDLE;
                }
            }
            else
            {
                const VkBuffer vb = batch.mesh->vertexBuffer;
                if (vb != boundVB)
                {
                    const VkDeviceSize off = 0;
                    vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vb, &off);
                    boundVB = vb;
                }
                const VkBuffer ib = batch.mesh->indexBuffer;
                if (ib != boundIB)
                {
                    vkCmdBindIndexBuffer(cmdBuf, ib, 0, batch.mesh->indexType);
                    boundIB = ib;
                }
            }

            CapturePushConstant pc{batch.firstInstance, {0, 0, 0}, m_perFrameData.elapsedTime};
            vkCmdPushConstants(cmdBuf, pipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(CapturePushConstant), &pc);

            const uint32_t firstIndex = useUnified ? batch.mesh->unifiedFirstIndex : 0u;
            const int32_t vertexOffset = useUnified ? batch.mesh->unifiedVertexOffset : 0;
            vkCmdDrawIndexed(cmdBuf, batch.mesh->indicesCount, batch.instanceCount,
                             firstIndex, vertexOffset, 0);
        }

        vkCmdEndRendering(cmdBuf);
    }

    // Transition cubemap → SHADER_READ_ONLY_OPTIMAL
    {
        auto barrier = utilities::ImageUtilities::insertImageMemoryBarrier(
            *captureImage,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6});
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmdBuf, &dep);
    }

    cmdBuf->end();
    cmdBuf->submit(ctx->getGraphicsQueue());
    vkQueueWaitIdle(ctx->getGraphicsQueue());

    cleanup();

    // ── Final cube image view ─────────────────────────────────────────────────
    VkImageView cubeView = VK_NULL_HANDLE;
    {
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ci.image = captureImage->vk();
        ci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        ci.format = kColorFmt;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
        vkCreateImageView(m_device, &ci, nullptr, &cubeView);
    }

    auto sampler = core::Sampler::createShared(
        VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK, VK_COMPARE_OP_ALWAYS,
        VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_FALSE, 1.0f);

    return {captureImage, cubeView, sampler};
}

void RenderGraph::prepareFrame(Camera::SharedPtr camera, Scene *scene, float deltaTime)
{
    if (const VkResult waitResult = vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
        waitResult != VK_SUCCESS)
    {
        VX_ENGINE_ERROR_STREAM("Failed to wait for frame fence before CPU-side frame preparation: "
                               << core::helpers::vulkanResultToString(waitResult) << '\n');
        return;
    }

    m_perFrameData.swapChainViewport = m_swapchain->getViewport();
    m_perFrameData.swapChainScissor = m_swapchain->getScissor();
    m_perFrameData.cameraDescriptorSet = m_cameraDescriptorSets[m_currentFrame];
    m_perFrameData.previewCameraDescriptorSet = m_previewCameraDescriptorSets[m_currentFrame];
    m_perFrameData.deltaTime = deltaTime;
    m_perFrameData.elapsedTime += deltaTime;
    m_perFrameData.unifiedStaticVertexBuffer = m_staticUnifiedGeometry.getVertexBuffer();
    m_perFrameData.unifiedStaticIndexBuffer = m_staticUnifiedGeometry.getIndexBuffer();

    CameraUBO cameraUBO{};

    cameraUBO.view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
    cameraUBO.projection = camera ? camera->getProjectionMatrix() : glm::mat4(1.0f);
    cameraUBO.projection[1][1] *= -1;
    cameraUBO.invProjection = glm::inverse(cameraUBO.projection);
    cameraUBO.invView = glm::inverse(cameraUBO.view);

    m_perFrameData.projection = cameraUBO.projection;
    m_perFrameData.view = cameraUBO.view;

    std::memcpy(m_cameraMapped[m_currentFrame], &cameraUBO, sizeof(CameraUBO));

    CameraUBO previewCameraUBO{};
    previewCameraUBO.view = glm::lookAt(
        glm::vec3(0, 0, 3),
        glm::vec3(0, 0, 0),
        glm::vec3(0, 1, 0));

    previewCameraUBO.projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 10.0f);
    previewCameraUBO.projection[1][1] *= -1;
    previewCameraUBO.invProjection = glm::inverse(previewCameraUBO.projection);
    previewCameraUBO.invView = glm::inverse(previewCameraUBO.view);

    m_perFrameData.previewProjection = previewCameraUBO.projection;
    m_perFrameData.previewView = previewCameraUBO.view;
    m_perFrameData.skyboxHDRPath = scene ? scene->getSkyboxHDRPath() : std::string{};
    m_perFrameData.fogSettings = scene ? scene->getFogSettings() : FogSettings{};
    m_perFrameData.fogSettingsHash = hashFogSettings(m_perFrameData.fogSettings);

    std::memcpy(m_previewCameraMapped[m_currentFrame], &previewCameraUBO, sizeof(CameraUBO));

    auto perFrameWorker = PerFrameDataWorker::begin(
        m_perFrameData,
        PerFrameDataWorker::Dependencies{
            .meshGeometryRegistry = &m_meshGeometryRegistry,
            .materialResolver = &m_sceneMaterialResolver,
            .bindlessRegistry = &m_bindlessRegistry,
            .rayTracingScene = &m_rayTracingScene,
            .rayTracingGeometryCache = &m_rayTracingGeometryCache,
            .skinnedBlasBuilder = &m_skinnedBlasBuilder,
            .lastFrustumPlanes = &m_lastFrustumPlanes,
            .lastFrustumCullingEnabled = &m_lastFrustumCullingEnabled,
            .currentFrame = m_currentFrame,
            .enableCpuSmallFeatureCulling = m_enableCpuSmallFeatureCulling});
    perFrameWorker.buildLightData(scene, camera.get());

    void *mapped = nullptr;
    m_lightSSBOs[m_currentFrame]->map(mapped);
    LightSSBO *ssboData = static_cast<LightSSBO *>(mapped);
    const auto &lights = perFrameWorker.getLightData();
    ssboData->lightCount = static_cast<int>(lights.size());
    if (!lights.empty())
        std::memcpy(ssboData->lights, lights.data(), lights.size() * sizeof(RenderGraphLightData));
    m_lightSSBOs[m_currentFrame]->unmap();
    const auto &lightSpaceMatrixUBO = perFrameWorker.getLightSpaceMatrixUBO();
    std::memcpy(m_lightMapped[m_currentFrame], &lightSpaceMatrixUBO, sizeof(RenderGraphLightSpaceMatrixUBO));

    const bool enableCpuFrustumCulling = (camera != nullptr) && m_enableCpuFrustumCulling;
    prepareFrameDataFromScene(scene, cameraUBO.view, cameraUBO.projection, enableCpuFrustumCulling);

    // New textures/materials are discovered while walking the scene, so sync the
    // current frame's bindless descriptor set only after that work is complete
    // and after its fence has been waited.
    m_bindlessRegistry.syncFrame(m_currentFrame);
    m_bindlessRegistry.uploadMaterialParams(m_currentFrame, m_bindlessRegistry.getRegisteredMaterialCount());
    m_perFrameData.bindlessDescriptorSet = m_bindlessRegistry.getBindlessSet(m_currentFrame);
}

void RenderGraph::createDescriptorSetPool()
{
    const std::vector<VkDescriptorPoolSize> poolSizes{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 32},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100}};

    // Camera + preview camera + per-object + shadow-per-object descriptor sets per frame.
    static constexpr uint32_t kRenderGraphSetGroupsPerFrame = 4;
    const uint32_t maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * kRenderGraphSetGroupsPerFrame;

    m_descriptorPool = core::DescriptorPool::createShared(m_device, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                                                          maxSets);
}

void RenderGraph::recreateSwapChain()
{
    for (const auto &[_, renderPass] : m_renderGraphPasses)
    {
        if (renderPass.enabled && renderPass.renderGraphPass)
            renderPass.renderGraphPass->freeResources();
    }

    m_swapchain->recreate();

    auto barriers = m_renderGraphPassesCompiler.onSwapChainResized(m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

    auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer.begin();

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep.pImageMemoryBarriers = barriers.data();

    vkCmdPipelineBarrier2(commandBuffer, &dep);

    commandBuffer.end();

    commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
    {
        std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    }

    for (const auto &[id, renderPass] : m_renderGraphPasses)
        if (renderPass.enabled)
            renderPass.renderGraphPass->compile(m_renderGraphPassesStorage);

    invalidateAllExecutionCaches();
}

bool RenderGraph::begin()
{
    utilities::AsyncGpuUpload::collectFinished(m_device);
    m_renderGraphProfiling->syncDetailedProfilingMode();

    const bool detailedProfilingEnabled = m_renderGraphProfiling->isDetailedProfilingEnabled();

    const VkResult waitForFenceResult = m_renderGraphProfiling->measureCpuStage(m_currentFrame, RenderGraphProfiling::CpuStage::WaitForFence,
                                                                                [&]()
                                                                                {
                                                                                    return vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);
                                                                                });

    const double currentFenceWaitMs = m_renderGraphProfiling->getCpuStageProfilingDataByFrame(m_currentFrame).waitForFenceMs;

    if (waitForFenceResult != VK_SUCCESS)
    {
        m_renderGraphProfiling->onFenceWaitFailure(m_currentFrame, currentFenceWaitMs);
        VX_ENGINE_ERROR_STREAM("Failed to wait for fences: " << core::helpers::vulkanResultToString(waitForFenceResult) << '\n');
        return false;
    }

    // Resize/fullscreen events can arrive while UI code is building a frame.
    // Recreate swapchain only from here, after fence wait, to avoid re-entrant resize work.
    if (m_presentToSwapchain && m_swapchainResizeRequested.exchange(false, std::memory_order_relaxed))
        recreateSwapChain();

    if (!m_uploadWaitSemaphoresByFrame[m_currentFrame].empty())
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, m_uploadWaitSemaphoresByFrame[m_currentFrame]);
        m_uploadWaitSemaphoresByFrame[m_currentFrame].clear();
    }

    if (m_presentToSwapchain && m_swapchain)
    {
        const bool desiredVSyncState = RenderQualitySettings::getInstance().enableVSync;
        if (m_swapchain->isVSyncEnabled() != desiredVSyncState)
        {
            m_swapchain->setVSyncEnabled(desiredVSyncState);
            recreateSwapChain();
            return false;
        }
    }

    if (VkResult result = vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]); result != VK_SUCCESS)
    {
        VX_ENGINE_ERROR_STREAM("Failed to reset fences: " << core::helpers::vulkanResultToString(result) << '\n');
        return false;
    }

    refreshCameraDescriptorSet(m_currentFrame);
    m_perFrameData.cameraDescriptorSet = m_cameraDescriptorSets[m_currentFrame];
    m_perFrameData.previewCameraDescriptorSet = m_previewCameraDescriptorSets[m_currentFrame];

    // Resolve profiling for the previous use of this frame slot, then reset and seed
    // current CPU stage timings (fence wait + resolve profiling).
    m_renderGraphProfiling->beginFrameCpuProfiling(m_currentFrame, currentFenceWaitMs);

    m_renderGraphProfiling->measureCpuStage(
        m_currentFrame,
        RenderGraphProfiling::CpuStage::CommandPoolReset,
        [&]()
        {
            m_commandPools[m_currentFrame]->reset(0);
            for (const auto &secondaryPool : m_secondaryCommandPools[m_currentFrame])
            {
                if (secondaryPool)
                    secondaryPool->reset(0);
            }
        });

    if (m_presentToSwapchain)
    {
        VkResult result = m_renderGraphProfiling->measureCpuStage(
            m_currentFrame,
            RenderGraphProfiling::CpuStage::AcquireImage,
            [&]()
            {
                return vkAcquireNextImageKHR(m_device, m_swapchain->vk(), UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &m_imageIndex);
            });

        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain();

            return false;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            VX_ENGINE_ERROR_STREAM("Failed to acquire swap chain image: " << core::helpers::vulkanResultToString(result) << '\n');
            return false;
        }
    }
    else
    {
        m_renderGraphProfiling->recordCpuStage(m_currentFrame, RenderGraphProfiling::CpuStage::AcquireImage, 0.0);
        const uint32_t imageCount = std::max(1u, m_swapchain->getImageCount());
        m_imageIndex = m_currentFrame % imageCount;
    }

    for (const auto &[id, renderGraphPass] : m_renderGraphPasses)
    {
        if (auto *shadowPass = dynamic_cast<ShadowRenderGraphPass *>(renderGraphPass.renderGraphPass.get()))
            shadowPass->syncActiveShadowCounts(m_perFrameData.activeSpotShadowCount, m_perFrameData.activePointShadowCount);
    }

    m_renderGraphProfiling->measureCpuStage(
        m_currentFrame,
        RenderGraphProfiling::CpuStage::Recompile,
        [&]()
        { recompileDirtyPasses(); });

    m_passContextData.currentFrame = m_currentFrame;
    m_passContextData.currentImageIndex = m_imageIndex;
    m_passContextData.executionIndex = 0;
    m_passContextData.activeDirectionalShadowCount = m_perFrameData.activeDirectionalCascadeCount;
    m_passContextData.activeSpotShadowCount = m_perFrameData.activeSpotShadowCount;
    m_passContextData.activePointShadowCount = m_perFrameData.activePointShadowCount;
    m_passContextData.activeRTShadowLayerCount = m_perFrameData.activeRTShadowLayerCount;

    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
        renderGraphPass->prepareRecord(m_perFrameData, m_passContextData);

    // Some passes may stream textures while recompiling / preparing (for example
    // skyboxes or probe assets). Submit those queued uploads before we record
    // this frame's draw work so the same-frame render submission sees them in
    // queue order instead of sampling images that are still in UNDEFINED.
    utilities::AsyncGpuUpload::batchFlush(core::VulkanContext::getContext()->getGraphicsQueue());

    // Ensure the cache is sized to match the current sorted pass list.
    if (m_passExecutionCache.size() != m_sortedRenderGraphPasses.size())
        m_passExecutionCache.resize(m_sortedRenderGraphPasses.size());

    struct PendingRenderExecution
    {
        IRenderGraphPass *pass{nullptr};
        core::CommandBuffer::SharedPtr secondaryCommandBuffer{nullptr};
        const IRenderGraphPass::RenderPassExecution *execution{nullptr};
        const std::vector<VkImageMemoryBarrier2> *preBarriers{nullptr};
        const std::vector<VkImageMemoryBarrier2> *postBarriers{nullptr};
        uint32_t executionIndex{0u};
        std::string passName;
        RenderGraphProfiling::PassExecutionProfilingData profilingData{};
        std::exception_ptr recordException{nullptr};
    };

    const auto recordPendingExecution = [&](PendingRenderExecution &pending)
    {
        const auto &execution = *pending.execution;

        try
        {
            if (execution.mode == IRenderGraphPass::ExecutionMode::DynamicRendering)
            {
                VkCommandBufferInheritanceRenderingInfo inheritanceRenderingInfo{
                    VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO};
                inheritanceRenderingInfo.colorAttachmentCount = static_cast<uint32_t>(execution.colorsRenderingItems.size());
                inheritanceRenderingInfo.pColorAttachmentFormats = execution.colorFormats.data();
                inheritanceRenderingInfo.depthAttachmentFormat = execution.depthFormat;
                inheritanceRenderingInfo.viewMask = 0;
                inheritanceRenderingInfo.rasterizationSamples = execution.rasterizationSamples;
                inheritanceRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

                VkCommandBufferInheritanceInfo inheritanceInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
                inheritanceInfo.pNext = &inheritanceRenderingInfo;
                inheritanceInfo.subpass = 0;

                if (!pending.secondaryCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, &inheritanceInfo))
                    throw std::runtime_error("Failed to begin secondary command buffer");
            }
            else
            {
                VkCommandBufferInheritanceInfo inheritanceInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
                if (!pending.secondaryCommandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, &inheritanceInfo))
                    throw std::runtime_error("Failed to begin secondary command buffer");
            }

            RenderGraphPassContext executionContext = m_passContextData;
            executionContext.executionIndex = pending.executionIndex;

            if (detailedProfilingEnabled)
            {
                profiling::ScopedDrawCallCounter scopedDrawCallCounter(pending.profilingData.drawCalls);
                pending.pass->record(pending.secondaryCommandBuffer, m_perFrameData, executionContext);
            }
            else
            {
                pending.pass->record(pending.secondaryCommandBuffer, m_perFrameData, executionContext);
            }

            if (!pending.secondaryCommandBuffer->end())
                throw std::runtime_error("Failed to end secondary command buffer");
        }
        catch (...)
        {
            pending.recordException = std::current_exception();
        }
    };

    std::vector<PendingRenderExecution> pendingExecutions;
    pendingExecutions.reserve(MAX_RENDER_JOBS);

    size_t passIndex = 0;
    size_t secIndex = 0;
    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
    {
        auto &cachedData = m_passExecutionCache[passIndex];
        const bool cacheHit = cachedData.valid &&
                              cachedData.imageIndex == m_passContextData.currentImageIndex &&
                              cachedData.directionalShadowCount == m_passContextData.activeDirectionalShadowCount &&
                              cachedData.spotShadowCount == m_passContextData.activeSpotShadowCount &&
                              cachedData.pointShadowCount == m_passContextData.activePointShadowCount &&
                              cachedData.executionCacheKey == renderGraphPass->getExecutionCacheKey(m_passContextData);
        if (!cacheHit)
            buildExecutionCacheForPass(passIndex);

        const auto &executions = cachedData.executions;
        for (size_t executionIndex = 0; executionIndex < executions.size(); ++executionIndex)
        {
            if (secIndex >= m_secondaryCommandBuffers[m_currentFrame].size())
                throw std::runtime_error("Not enough secondary command buffers preallocated for frame");

            PendingRenderExecution pending{};
            pending.pass = renderGraphPass;
            pending.secondaryCommandBuffer = m_secondaryCommandBuffers[m_currentFrame][secIndex];
            pending.execution = &executions[executionIndex];
            pending.preBarriers = &cachedData.preBarriers[executionIndex];
            pending.postBarriers = &cachedData.postBarriers[executionIndex];
            pending.executionIndex = static_cast<uint32_t>(executionIndex);
            pending.passName = renderGraphPass->getDebugName().empty() ? ("Pass " + std::to_string(passIndex))
                                                                       : renderGraphPass->getDebugName();
            pendingExecutions.push_back(std::move(pending));
            ++secIndex;
        }

        ++passIndex;
    }

    std::vector<size_t> parallelExecutionIndices;
    std::vector<size_t> serialExecutionIndices;
    parallelExecutionIndices.reserve(pendingExecutions.size());
    serialExecutionIndices.reserve(pendingExecutions.size());

    for (size_t pendingIndex = 0; pendingIndex < pendingExecutions.size(); ++pendingIndex)
    {
        if (pendingExecutions[pendingIndex].pass->canRecordInParallel())
            parallelExecutionIndices.push_back(pendingIndex);
        else
            serialExecutionIndices.push_back(pendingIndex);
    }

    if (!parallelExecutionIndices.empty())
    {
        const size_t recordingThreadCount = std::min<std::size_t>(parallelExecutionIndices.size(), ThreadPoolManager::instance().getMaxThreads());
        ThreadPoolManager::instance().parallelFor(
            parallelExecutionIndices.size(),
            [&](std::size_t beginIndex, std::size_t endIndex)
            {
                for (std::size_t rangeIndex = beginIndex; rangeIndex < endIndex; ++rangeIndex)
                {
                    auto &pending = pendingExecutions[parallelExecutionIndices[rangeIndex]];
                    recordPendingExecution(pending);
                }
            },
            recordingThreadCount);
    }

    for (const size_t pendingIndex : serialExecutionIndices)
        recordPendingExecution(pendingExecutions[pendingIndex]);

    const auto &primaryCommandBuffer = m_commandBuffers.at(m_currentFrame);

    primaryCommandBuffer->begin();

    // GPU frustum culling — zeroes instanceCount for batches outside the view frustum.
    // Must run before any render pass that reads VkDrawIndexedIndirectCommand[].
    m_gpuCulling.dispatch(primaryCommandBuffer->vk(), m_currentFrame,
                          static_cast<uint32_t>(m_perFrameData.drawBatches.size()),
                          m_lastFrustumPlanes);

    m_renderGraphProfiling->beginFrameGpuProfiling(primaryCommandBuffer->vk(), m_currentFrame);

    for (auto &pending : pendingExecutions)
    {
        if (pending.recordException)
            std::rethrow_exception(pending.recordException);

        const auto &execution = *pending.execution;

        m_renderGraphProfiling->beginPassProfiling(primaryCommandBuffer->vk(), m_currentFrame, pending.profilingData, pending.passName);

        std::chrono::high_resolution_clock::time_point cpuStartTime{};
        if (detailedProfilingEnabled)
            cpuStartTime = std::chrono::high_resolution_clock::now();

        VkDependencyInfo firstDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        firstDep.imageMemoryBarrierCount = static_cast<uint32_t>(pending.preBarriers->size());
        firstDep.pImageMemoryBarriers = pending.preBarriers->data();
        vkCmdPipelineBarrier2(primaryCommandBuffer, &firstDep);

        if (execution.mode == IRenderGraphPass::ExecutionMode::DynamicRendering)
        {
            VkRect2D safeRenderArea = execution.renderArea;
            sanitizeExecutionRenderArea(safeRenderArea, getExecutionMinAttachmentExtent(execution));

            VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
            renderingInfo.renderArea = safeRenderArea;
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(execution.colorsRenderingItems.size());
            renderingInfo.pColorAttachments = execution.colorsRenderingItems.data();
            renderingInfo.pDepthAttachment = execution.useDepth ? &execution.depthRenderingItem : VK_NULL_HANDLE;
            renderingInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;

            vkCmdBeginRendering(primaryCommandBuffer, &renderingInfo);
            vkCmdExecuteCommands(primaryCommandBuffer->vk(), 1, pending.secondaryCommandBuffer->pVk());
            vkCmdEndRendering(primaryCommandBuffer->vk());
        }
        else
        {
            vkCmdExecuteCommands(primaryCommandBuffer->vk(), 1, pending.secondaryCommandBuffer->pVk());
        }

        VkDependencyInfo secondDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        secondDep.imageMemoryBarrierCount = static_cast<uint32_t>(pending.postBarriers->size());
        secondDep.pImageMemoryBarriers = pending.postBarriers->data();
        vkCmdPipelineBarrier2(primaryCommandBuffer, &secondDep);

        if (detailedProfilingEnabled)
        {
            const auto cpuEndTime = std::chrono::high_resolution_clock::now();
            pending.profilingData.cpuTimeMs = std::chrono::duration<double, std::milli>(cpuEndTime - cpuStartTime).count();
        }

        m_renderGraphProfiling->endPassProfiling(primaryCommandBuffer->vk(), m_currentFrame, std::move(pending.profilingData));
    }

    m_renderGraphProfiling->endFrameGpuProfiling(primaryCommandBuffer->vk(), m_currentFrame);

    m_renderGraphProfiling->measureCpuStage(
        m_currentFrame,
        RenderGraphProfiling::CpuStage::PrimaryCommandBufferEnd,
        [&]()
        { primaryCommandBuffer->end(); });

    return true;
}

void RenderGraph::end()
{
    const auto &currentCommandBuffer = m_commandBuffers.at(m_currentFrame);
    std::vector<core::CommandBuffer::SemaphoreSubmitDesc> waitSemaphores;
    std::vector<core::CommandBuffer::SemaphoreSubmitDesc> signalSemaphoreDescs;
    std::vector<VkSemaphore> signalSemaphores;
    const std::vector<VkSwapchainKHR> swapChains = {m_swapchain->vk()};

    if (m_presentToSwapchain)
    {
        waitSemaphores.push_back(core::CommandBuffer::SemaphoreSubmitDesc{
            .semaphore = m_imageAvailableSemaphores[m_currentFrame],
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        });
        signalSemaphoreDescs.push_back(core::CommandBuffer::SemaphoreSubmitDesc{
            .semaphore = m_renderFinishedSemaphores[m_currentFrame],
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        });
        signalSemaphores.push_back(m_renderFinishedSemaphores[m_currentFrame]);
    }

    utilities::AsyncGpuUpload::collectFinished(m_device);
    const auto uploadTimelineWait = utilities::AsyncGpuUpload::acquireReadyTimelineWait();
    if (uploadTimelineWait.semaphore != VK_NULL_HANDLE && uploadTimelineWait.value > 0u)
    {
        waitSemaphores.push_back(core::CommandBuffer::SemaphoreSubmitDesc{
            .semaphore = uploadTimelineWait.semaphore,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .value = uploadTimelineWait.value,
        });
    }

    auto uploadWaitSemaphores = utilities::AsyncGpuUpload::acquireReadySemaphores();
    for (VkSemaphore uploadSemaphore : uploadWaitSemaphores)
    {
        waitSemaphores.push_back(core::CommandBuffer::SemaphoreSubmitDesc{
            .semaphore = uploadSemaphore,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        });
    }

    const bool submitOk = m_renderGraphProfiling->measureCpuStage(
        m_currentFrame,
        RenderGraphProfiling::CpuStage::Submit,
        [&]()
        {
            return currentCommandBuffer->submit2(core::VulkanContext::getContext()->getGraphicsQueue(),
                                                 waitSemaphores,
                                                 signalSemaphoreDescs,
                                                 m_inFlightFences[m_currentFrame]);
        });

    if (!submitOk)
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, uploadWaitSemaphores);
        throw std::runtime_error("Failed to submit render graph command buffer");
    }

    m_uploadWaitSemaphoresByFrame[m_currentFrame] = std::move(uploadWaitSemaphores);

    m_renderGraphProfiling->markFrameSubmitted(m_currentFrame);

    if (m_presentToSwapchain)
    {
        VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        presentInfo.waitSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        presentInfo.pWaitSemaphores = signalSemaphores.data();
        presentInfo.swapchainCount = static_cast<uint32_t>(swapChains.size());
        presentInfo.pSwapchains = swapChains.data();
        presentInfo.pImageIndices = &m_imageIndex;
        presentInfo.pResults = nullptr;

        VkResult result = m_renderGraphProfiling->measureCpuStage(
            m_currentFrame,
            RenderGraphProfiling::CpuStage::Present,
            [&]()
            {
                std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
                return vkQueuePresentKHR(core::VulkanContext::getContext()->getPresentQueue(), &presentInfo);
            });

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            recreateSwapChain();
        else if (result != VK_SUCCESS)
            throw std::runtime_error("Failed to present swap chain image: " + core::helpers::vulkanResultToString(result));
    }
    else
        m_renderGraphProfiling->recordCpuStage(m_currentFrame, RenderGraphProfiling::CpuStage::Present, 0.0);
}

void RenderGraph::draw()
{
    m_renderGraphProfiling->measureFrameCpuTime(
        m_currentFrame,
        [&]()
        {
            if (begin())
                end();
        });

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void RenderGraph::setup()
{
    std::vector<RenderGraphPassData *> setupOrder;
    setupOrder.reserve(m_renderGraphPasses.size());

    for (auto &[_, pass] : m_renderGraphPasses)
        setupOrder.push_back(&pass);

    std::sort(setupOrder.begin(), setupOrder.end(), [](const RenderGraphPassData *a, const RenderGraphPassData *b)
              { return a->id < b->id; });

    for (auto *passData : setupOrder)
    {
        if (!passData)
        {
            VX_ENGINE_ERROR_STREAM("failed to find render graph pass during setup\n");
            continue;
        }

        const std::string &debugName = passData->renderGraphPass->getDebugName();
        VX_ENGINE_INFO_STREAM("[RenderGraph] Setup pass " << passData->id << ": "
                                                         << (debugName.empty() ? "<unnamed>" : debugName) << '\n');

        m_renderGraphPassesBuilder.setCurrentPass(&passData->passInfo);
        passData->renderGraphPass->setup(m_renderGraphPassesBuilder);
    }

    compile();
}

void RenderGraph::sortRenderGraphPasses()
{
    auto producerConsumer = [](const RenderGraphPassData &a, const RenderGraphPassData &b)
    {
        for (const auto &wa : a.passInfo.writes)
            for (const auto &rb : b.passInfo.reads)
                if (wa.resourceId == rb.resourceId)
                    return true;
        return false;
    };

    for (auto &[_, renderGraphPass] : m_renderGraphPasses)
    {
        renderGraphPass.outgoing.clear();
        renderGraphPass.indegree = 0;
    }

    auto addEdge = [&](uint32_t srcId, uint32_t dstId)
    {
        if (srcId == dstId)
            return;

        auto *srcPass = findRenderGraphPassById(srcId);
        auto *dstPass = findRenderGraphPassById(dstId);

        if (!srcPass || !dstPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to add graph edge " << srcId << " -> " << dstId << '\n');
            return;
        }

        if (std::find(srcPass->outgoing.begin(), srcPass->outgoing.end(), dstId) != srcPass->outgoing.end())
            return;

        srcPass->outgoing.push_back(dstId);
        dstPass->indegree++;
    };

    for (auto &[_, renderGraphPass] : m_renderGraphPasses)
    {
        for (auto &[_, secondRenderGraphPass] : m_renderGraphPasses)
        {
            if (renderGraphPass.id == secondRenderGraphPass.id)
                continue;

            if (producerConsumer(renderGraphPass, secondRenderGraphPass))
                addEdge(renderGraphPass.id, secondRenderGraphPass.id);
        }
    }

    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> q;
    std::vector<uint32_t> sorted;

    for (auto &[id, renderGraphPass] : m_renderGraphPasses)
        if (renderGraphPass.indegree <= 0)
            q.push(renderGraphPass.id);

    while (!q.empty())
    {
        uint32_t n = q.top();
        q.pop();
        sorted.push_back(n);

        auto renderGraphPass = findRenderGraphPassById(n);

        if (!renderGraphPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to find pass. Error...\n");
            continue;
        }

        for (uint32_t dst : renderGraphPass->outgoing)
        {
            auto dstRenderGraphPass = findRenderGraphPassById(dst);

            if (!dstRenderGraphPass)
            {
                VX_ENGINE_ERROR_STREAM("Failed to find dst pass. Error...\n");
                continue;
            }

            if (--dstRenderGraphPass->indegree == 0)
                q.push(dst);
        }
    }

    if (sorted.size() != m_renderGraphPasses.size())
    {
        VX_ENGINE_ERROR_STREAM("Failed to build graph tree\n");
        return;
    }

    m_sortedRenderGraphPasses.clear();
    m_sortedRenderGraphPassIds.clear();
    m_sortedRenderGraphPasses.reserve(sorted.size());
    m_sortedRenderGraphPassIds.reserve(sorted.size());

    for (const auto &sortId : sorted)
    {
        auto renderGraphPass = findRenderGraphPassById(sortId);

        if (!renderGraphPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to find sorted node\n");
            continue;
        }

        if (!renderGraphPass->enabled)
            continue; // Skip permanently-disabled passes (VRAM freed via disablePass<T>())

        m_sortedRenderGraphPassIds.push_back(sortId);
        m_sortedRenderGraphPasses.push_back(renderGraphPass->renderGraphPass.get());
    }

    m_passExecutionCache.assign(m_sortedRenderGraphPasses.size(), CachedPassExecutionData{});

    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
        VX_ENGINE_INFO_STREAM("Node: " << renderGraphPass->getDebugName());
}

std::unordered_map<RGPResourceHandler, RGPResourceHandler> RenderGraph::buildAliasedTextureRoots()
{
    struct TextureLifetime
    {
        RGPResourceHandler handler{};
        TextureAliasSignature signature{};
        int32_t firstUse{std::numeric_limits<int32_t>::max()};
        int32_t lastUse{-1};
        bool initialized{false};
        bool aliasable{true};
    };

    struct AliasSlot
    {
        RGPResourceHandler root{};
        int32_t lastUse{-1};
    };

    std::unordered_map<RGPResourceHandler, TextureLifetime> lifetimes;
    lifetimes.reserve(m_renderGraphPasses.size() * 4);

    for (size_t passIndex = 0; passIndex < m_sortedRenderGraphPassIds.size(); ++passIndex)
    {
        auto *passData = findRenderGraphPassById(m_sortedRenderGraphPassIds[passIndex]);
        if (!passData || !passData->enabled)
            continue;

        const auto updateLifetime = [&](const RGPResourceAccess &access)
        {
            const auto *textureDescription = m_renderGraphPassesBuilder.getTextureDescription(access.resourceId);
            if (!textureDescription || textureDescription->getIsSwapChainTarget() || !textureDescription->getAliasable())
                return;

            TextureAliasSignature signature{};
            signature.extent = textureDescription->getCustomExtentFunction() ? textureDescription->getCustomExtentFunction()() : textureDescription->getExtent();
            signature.usage = textureDescription->getUsage();
            signature.format = textureDescription->getFormat();
            signature.initialLayout = textureDescription->getInitialLayout();
            signature.finalLayout = textureDescription->getFinalLayout();
            signature.sampleCount = textureDescription->getSampleCount();
            signature.arrayLayers = textureDescription->getArrayLayers();
            signature.flags = textureDescription->getFlags();
            signature.viewType = textureDescription->getImageViewtype();

            auto &lifetime = lifetimes[access.resourceId];
            if (!lifetime.initialized)
            {
                lifetime.handler = access.resourceId;
                lifetime.signature = signature;
                lifetime.firstUse = static_cast<int32_t>(passIndex);
                lifetime.lastUse = static_cast<int32_t>(passIndex);
                lifetime.initialized = true;
                lifetime.aliasable = true;
                return;
            }

            if (!(lifetime.signature == signature))
            {
                lifetime.aliasable = false;
                return;
            }

            lifetime.lastUse = std::max(lifetime.lastUse, static_cast<int32_t>(passIndex));
        };

        for (const auto &read : passData->passInfo.reads)
            updateLifetime(read);
        for (const auto &write : passData->passInfo.writes)
            updateLifetime(write);
    }

    std::vector<TextureLifetime> candidates;
    candidates.reserve(lifetimes.size());
    for (const auto &[_, lifetime] : lifetimes)
    {
        if (!lifetime.initialized || !lifetime.aliasable || lifetime.firstUse > lifetime.lastUse)
            continue;

        candidates.push_back(lifetime);
    }

    std::sort(candidates.begin(), candidates.end(), [](const TextureLifetime &a, const TextureLifetime &b)
              {
                  if (a.firstUse != b.firstUse)
                      return a.firstUse < b.firstUse;
                  if (a.lastUse != b.lastUse)
                      return a.lastUse < b.lastUse;
                  return a.handler.id < b.handler.id; });

    std::unordered_map<TextureAliasSignature, std::vector<AliasSlot>, TextureAliasSignatureHasher> slotsBySignature;
    std::unordered_map<RGPResourceHandler, RGPResourceHandler> aliasRoots;
    aliasRoots.reserve(candidates.size());

    for (const auto &candidate : candidates)
    {
        auto &slots = slotsBySignature[candidate.signature];
        auto reusableIt = std::find_if(slots.begin(), slots.end(), [&](const AliasSlot &slot)
                                       { return slot.lastUse < candidate.firstUse; });

        if (reusableIt == slots.end())
        {
            aliasRoots[candidate.handler] = candidate.handler;
            slots.push_back(AliasSlot{candidate.handler, candidate.lastUse});
            continue;
        }

        aliasRoots[candidate.handler] = reusableIt->root;
        reusableIt->lastUse = candidate.lastUse;
    }

    return aliasRoots;
}

void RenderGraph::invalidateAllExecutionCaches()
{
    m_passExecutionCache.assign(m_sortedRenderGraphPasses.size(), CachedPassExecutionData{});
}

void RenderGraph::buildExecutionCacheForPass(size_t sortedPassIndex)
{
    if (sortedPassIndex >= m_sortedRenderGraphPasses.size())
        return;

    auto &cached = m_passExecutionCache[sortedPassIndex];
    auto *pass = m_sortedRenderGraphPasses[sortedPassIndex];

    cached.executions = pass->getRenderPassExecutions(m_passContextData);
    for (auto &execution : cached.executions)
        normalizeExecutionAttachments(execution);
    cached.imageIndex = m_passContextData.currentImageIndex;
    cached.directionalShadowCount = m_passContextData.activeDirectionalShadowCount;
    cached.spotShadowCount = m_passContextData.activeSpotShadowCount;
    cached.pointShadowCount = m_passContextData.activePointShadowCount;
    cached.executionCacheKey = pass->getExecutionCacheKey(m_passContextData);

    const size_t execCount = cached.executions.size();
    cached.preBarriers.resize(execCount);
    cached.postBarriers.resize(execCount);

    for (size_t execIdx = 0; execIdx < execCount; ++execIdx)
    {
        const auto &exec = cached.executions[execIdx];
        auto &pre = cached.preBarriers[execIdx];
        auto &post = cached.postBarriers[execIdx];
        pre.clear();
        post.clear();
        pre.reserve(exec.targets.size());
        post.reserve(exec.targets.size());

        for (const auto &[id, target] : exec.targets)
        {
            auto textureDescription = m_renderGraphPassesBuilder.getTextureDescription(id);
            if (!textureDescription || !target)
                continue;

            auto srcInfoInitial = utilities::ImageUtilities::getSrcLayoutInfo(textureDescription->getInitialLayout());
            auto srcInfoFinal = utilities::ImageUtilities::getSrcLayoutInfo(textureDescription->getFinalLayout());
            auto dstInfoInitial = utilities::ImageUtilities::getDstLayoutInfo(textureDescription->getInitialLayout());
            auto dstInfoFinal = utilities::ImageUtilities::getDstLayoutInfo(textureDescription->getFinalLayout());
            auto aspect = utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription->getFormat());

            pre.push_back(utilities::ImageUtilities::insertImageMemoryBarrier(
                *target->getImage(),
                srcInfoFinal.accessMask,
                dstInfoInitial.accessMask,
                textureDescription->getFinalLayout(),
                textureDescription->getInitialLayout(),
                srcInfoFinal.stageMask,
                dstInfoInitial.stageMask,
                {aspect, 0, 1, 0, textureDescription->getArrayLayers()}));

            post.push_back(utilities::ImageUtilities::insertImageMemoryBarrier(
                *target->getImage(),
                srcInfoInitial.accessMask,
                dstInfoFinal.accessMask,
                textureDescription->getInitialLayout(),
                textureDescription->getFinalLayout(),
                srcInfoInitial.stageMask,
                dstInfoFinal.stageMask,
                {aspect, 0, 1, 0, textureDescription->getArrayLayers()}));
        }
    }

    cached.valid = true;
}

bool RenderGraph::recompileDirtyPasses()
{
    std::vector<uint32_t> dirtyPassIds;
    dirtyPassIds.reserve(m_renderGraphPasses.size());

    for (const auto &[id, renderGraphPass] : m_renderGraphPasses)
        if (renderGraphPass.renderGraphPass->needsRecompilation())
            dirtyPassIds.push_back(renderGraphPass.id);

    if (dirtyPassIds.empty())
        return false;

    {
        std::ostringstream stream;
        stream << "Render graph recompiling dirty passes:";
        for (const uint32_t dirtyPassId : dirtyPassIds)
        {
            if (auto *passData = findRenderGraphPassById(dirtyPassId); passData && passData->renderGraphPass)
                stream << ' ' << '[' << passData->renderGraphPass->getDebugName() << ']';
            else
                stream << " [unknown]";
        }
        VX_ENGINE_INFO_STREAM(stream.str() << '\n');
    }

    vkDeviceWaitIdle(m_device);
    compile();

    for (const uint32_t dirtyPassId : dirtyPassIds)
    {
        auto *passData = findRenderGraphPassById(dirtyPassId);
        if (passData)
            passData->renderGraphPass->recompilationIsDone();
    }

    return true;
}

void RenderGraph::compile()
{
    sortRenderGraphPasses();

    for (const auto &[_, renderPass] : m_renderGraphPasses)
    {
        if (renderPass.enabled && renderPass.renderGraphPass)
            renderPass.renderGraphPass->freeResources();
    }

    m_renderGraphPassesStorage.cleanup();

    const auto aliasRoots = buildAliasedTextureRoots();
    auto barriers = m_renderGraphPassesCompiler.compile(m_renderGraphPassesBuilder, m_renderGraphPassesStorage, &aliasRoots);

    auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer.begin();

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep.pImageMemoryBarriers = barriers.data();

    vkCmdPipelineBarrier2(commandBuffer, &dep);

    commandBuffer.end();

    commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
    {
        std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    }

    for (const auto &[id, pass] : m_renderGraphPasses)
        if (pass.enabled)
            pass.renderGraphPass->compile(m_renderGraphPassesStorage);

    invalidateAllExecutionCaches();

    if (m_presentToSwapchain && !m_hasWindowResizeCallback)
    {
        m_hasWindowResizeCallback = true;
        core::VulkanContext::getContext()->getSwapchain()->getWindow().addResizeCallback([this](platform::Window *, int, int)
                                                                                         { m_swapchainResizeRequested.store(true, std::memory_order_relaxed); });
    }
}

void RenderGraph::createPreviewCameraDescriptorSets()
{
    m_previewCameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_previewCameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_previewCameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &cameraBuffer = m_previewCameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_previewCameraMapped[i]);

        m_previewCameraDescriptorSets[i] = DescriptorSetBuilder::begin()
                                               .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                               .build(m_device, m_descriptorPool, EngineShaderFamilies::cameraDescriptorSetLayout->vk());
    }
}

void RenderGraph::refreshCameraDescriptorSet(uint32_t frameIndex)
{
    if (frameIndex >= MAX_FRAMES_IN_FLIGHT ||
        frameIndex >= m_cameraUniformObjects.size() ||
        frameIndex >= m_lightSpaceMatrixUniformObjects.size() ||
        frameIndex >= m_lightSSBOs.size() ||
        frameIndex >= m_cameraDescriptorSets.size())
        return;

    if (m_cameraDescriptorSets[frameIndex] != VK_NULL_HANDLE)
    {
        vkFreeDescriptorSets(m_device, m_descriptorPool->vk(), 1, &m_cameraDescriptorSets[frameIndex]);
        m_cameraDescriptorSets[frameIndex] = VK_NULL_HANDLE;
    }

    auto builder = DescriptorSetBuilder::begin()
                       .addBuffer(m_cameraUniformObjects[frameIndex], sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                       .addBuffer(m_lightSpaceMatrixUniformObjects[frameIndex], sizeof(RenderGraphLightSpaceMatrixUBO), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                       .addBuffer(m_lightSSBOs[frameIndex], VK_WHOLE_SIZE, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    auto context = core::VulkanContext::getContext();
    if (context && context->hasAccelerationStructureSupport())
    {
        const auto &tlas = m_rayTracingScene.getTLAS(frameIndex);
        if (tlas && tlas->isValid())
            builder.addAccelerationStructure(tlas->vk(), 3);
    }

    m_cameraDescriptorSets[frameIndex] = builder.build(m_device, m_descriptorPool, EngineShaderFamilies::cameraDescriptorSetLayout->vk());
}

void RenderGraph::createPerObjectDescriptorSets()
{
    m_perObjectDescriptorSets.resize(RenderGraph::MAX_FRAMES_IN_FLIGHT);
    m_shadowPerObjectDescriptorSets.resize(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    m_bonesSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);
    m_instanceSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);
    m_shadowInstanceSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    static constexpr VkDeviceSize bonesStructSize = sizeof(glm::mat4);
    static constexpr uint32_t INIT_BONES_COUNT = 1000u;
    static constexpr VkDeviceSize BONES_INITIAL_SIZE = bonesStructSize * INIT_BONES_COUNT;

    static constexpr VkDeviceSize instanceStructSize = sizeof(PerObjectInstanceData);
    static constexpr uint32_t INIT_INSTANCE_COUNT = 20000u;
    static constexpr VkDeviceSize INSTANCES_INITIAL_SIZE = instanceStructSize * INIT_INSTANCE_COUNT;
    static constexpr uint32_t INIT_SHADOW_INSTANCE_COUNT = 100000u;
    static constexpr VkDeviceSize SHADOW_INSTANCES_INITIAL_SIZE = instanceStructSize * INIT_SHADOW_INSTANCE_COUNT;

    for (size_t i = 0; i < RenderGraph::MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto bonesBuffer = m_bonesSSBOs.emplace_back(core::Buffer::createShared(BONES_INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                                core::memory::MemoryUsage::CPU_TO_GPU));
        auto instanceBuffer = m_instanceSSBOs.emplace_back(core::Buffer::createShared(INSTANCES_INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                                      core::memory::MemoryUsage::CPU_TO_GPU));
        auto shadowInstanceBuffer = m_shadowInstanceSSBOs.emplace_back(core::Buffer::createShared(SHADOW_INSTANCES_INITIAL_SIZE,
                                                                                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                                                  core::memory::MemoryUsage::CPU_TO_GPU));

        m_perObjectDescriptorSets[i] = DescriptorSetBuilder::begin()
                                           .addBuffer(bonesBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                           .addBuffer(instanceBuffer, VK_WHOLE_SIZE, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                           .build(core::VulkanContext::getContext()->getDevice(), m_descriptorPool, EngineShaderFamilies::objectDescriptorSetLayout->vk());

        m_shadowPerObjectDescriptorSets[i] = DescriptorSetBuilder::begin()
                                                 .addBuffer(bonesBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                                 .addBuffer(shadowInstanceBuffer, VK_WHOLE_SIZE, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                                 .build(core::VulkanContext::getContext()->getDevice(), m_descriptorPool, EngineShaderFamilies::objectDescriptorSetLayout->vk());
    }
}

void RenderGraph::createCameraDescriptorSets()
{
    m_cameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightMapped.resize(MAX_FRAMES_IN_FLIGHT);

    m_lightSpaceMatrixUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_lightSSBOs.reserve(MAX_FRAMES_IN_FLIGHT);

    static constexpr uint8_t INIT_LIGHTS_COUNT = 2;
    static constexpr VkDeviceSize INITIAL_SIZE = sizeof(RenderGraphLightData) * (INIT_LIGHTS_COUNT * sizeof(RenderGraphLightData));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &cameraBuffer = m_cameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto &lightBuffer = m_lightSpaceMatrixUniformObjects.emplace_back(core::Buffer::createShared(sizeof(RenderGraphLightSpaceMatrixUBO),
                                                                                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto ssboBuffer = m_lightSSBOs.emplace_back(core::Buffer::createShared(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                               core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_cameraMapped[i]);
        lightBuffer->map(m_lightMapped[i]);
        refreshCameraDescriptorSet(static_cast<uint32_t>(i));
    }

    createPreviewCameraDescriptorSets();
    createPerObjectDescriptorSets();
}

void RenderGraph::createRenderGraphResources()
{
    createDescriptorSetPool();
    createCameraDescriptorSets();

    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
    {
        auto commandPool = m_commandPools.emplace_back(core::CommandPool::createShared(core::VulkanContext::getContext()->getDevice(),
                                                                                       core::VulkanContext::getContext()->getGraphicsFamily()));

        m_commandBuffers.emplace_back(core::CommandBuffer::createShared(*commandPool));

        m_secondaryCommandPools[frame].resize(MAX_RENDER_JOBS);
        m_secondaryCommandBuffers[frame].resize(MAX_RENDER_JOBS);

        for (size_t job = 0; job < MAX_RENDER_JOBS; ++job)
        {
            auto secondaryPool = core::CommandPool::createShared(core::VulkanContext::getContext()->getDevice(),
                                                                 core::VulkanContext::getContext()->getGraphicsFamily());
            m_secondaryCommandPools[frame][job] = secondaryPool;
            m_secondaryCommandBuffers[frame][job] = core::CommandBuffer::createShared(*secondaryPool, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
        }
    }

    m_renderGraphProfiling = std::make_unique<RenderGraphProfiling>(static_cast<uint32_t>(m_renderGraphPasses.size()));

    sortRenderGraphPasses();
    m_renderGraphProfiling->syncDetailedProfilingMode();

    m_gpuCulling.initialize(m_device, MAX_FRAMES_IN_FLIGHT);
    m_bindlessRegistry.initialize(m_device, MAX_FRAMES_IN_FLIGHT);
}

void RenderGraph::cleanResources()
{
    utilities::AsyncGpuUpload::flush(m_device);

    {
        std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    }
    vkDeviceWaitIdle(m_device);

    for (auto &uploadSemaphores : m_uploadWaitSemaphoresByFrame)
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, uploadSemaphores);
        uploadSemaphores.clear();
    }

    utilities::AsyncGpuUpload::shutdown(m_device);

    for (const auto &primary : m_commandBuffers)
        primary->destroyVk();
    for (const auto &secondary : m_secondaryCommandBuffers)
        for (const auto &cb : secondary)
            cb->destroyVk();

    for (const auto &renderPass : m_renderGraphPasses)
        renderPass.second.renderGraphPass->cleanup();

    for (auto &semaphore : m_imageAvailableSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }

    for (auto &semaphore : m_renderFinishedSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }

    for (auto &fence : m_inFlightFences)
    {
        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
    }

    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_swapchainResizeRequested.store(false, std::memory_order_relaxed);
    m_hasWindowResizeCallback = false;

    m_renderGraphProfiling->destroyTimestampQueryPool();
    m_renderGraphPassesStorage.cleanup();
    m_rayTracingScene.clear();
    m_rayTracingGeometryCache.clear();
    m_skinnedBlasBuilder.clear();

    m_gpuCulling.cleanup(m_device);
    m_bindlessRegistry.cleanup(m_device);

    // Sentinel: prevents ~RenderGraph() from calling cleanResources() a second time.
    m_device = VK_NULL_HANDLE;
}

void RenderGraph::disablePassData(RenderGraphPassData &data)
{
    if (!data.enabled)
        return;

    // GPU sync — ensure no in-flight work touches this pass's resources.
    {
        std::lock_guard<std::mutex> queueLock(core::helpers::queueHostSyncMutex());
        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    }

    // Let the pass clean up its descriptor sets and cached RenderTarget* pointers.
    data.renderGraphPass->freeResources();

    // Free the VRAM owned by this pass (its write-side resources).
    for (const auto &access : data.passInfo.writes)
    {
        const auto *desc = m_renderGraphPassesBuilder.getTextureDescription(access.resourceId);
        if (!desc)
            continue;

        if (desc->getIsSwapChainTarget())
            m_renderGraphPassesStorage.removeSwapChainTexture(access.resourceId);
        else
            m_renderGraphPassesStorage.removeTexture(access.resourceId);
    }

    // Mark all downstream passes for recompile so they rebuild their descriptor sets
    // (their bindings to this pass's output images are now stale).
    for (const uint32_t downstreamId : data.outgoing)
    {
        auto *downstream = findRenderGraphPassById(downstreamId);
        if (downstream)
            downstream->renderGraphPass->requestRecompilation();
    }

    data.enabled = false;
    sortRenderGraphPasses(); // Rebuild sorted list excluding this pass
    invalidateAllExecutionCaches();
}

void RenderGraph::enablePassData(RenderGraphPassData &data)
{
    if (data.enabled)
        return;

    data.enabled = true;
    data.renderGraphPass->requestRecompilation();
    sortRenderGraphPasses(); // Rebuild sorted list including this pass again
    invalidateAllExecutionCaches();
}

void RenderGraph::registerGroup(PassGroup group)
{
    m_passGroups[group.name] = std::move(group);
}

void RenderGraph::disableGroup(const std::string &name)
{
    auto it = m_passGroups.find(name);
    if (it == m_passGroups.end())
        return;

    for (const auto &typeIdx : it->second.passes)
    {
        auto passIt = m_renderGraphPasses.find(typeIdx);
        if (passIt != m_renderGraphPasses.end())
            disablePassData(passIt->second);
    }
}

void RenderGraph::enableGroup(const std::string &name)
{
    auto it = m_passGroups.find(name);
    if (it == m_passGroups.end())
        return;

    for (const auto &typeIdx : it->second.passes)
    {
        auto passIt = m_renderGraphPasses.find(typeIdx);
        if (passIt != m_renderGraphPasses.end())
            enablePassData(passIt->second);
    }
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
