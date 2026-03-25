#include "Engine/Render/GpuCullingSystem.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/ShaderHandler.hpp"

#include <glm/glm.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

std::array<glm::vec4, 6> GpuCullingSystem::extractFrustumPlanes(const glm::mat4 &viewProj)
{
    std::array<glm::vec4, 6> planes{};

    auto row = [&](int r) -> glm::vec4
    {
        return glm::vec4(viewProj[0][r], viewProj[1][r], viewProj[2][r], viewProj[3][r]);
    };

    planes[0] = row(3) + row(0); // Left
    planes[1] = row(3) - row(0); // Right
    planes[2] = row(3) + row(1); // Bottom
    planes[3] = row(3) - row(1); // Top
    planes[4] = row(2);          // Near (Vulkan: z_clip >= 0)
    planes[5] = row(3) - row(2); // Far

    for (auto &p : planes)
    {
        const float len = glm::length(glm::vec3(p));
        if (len > 1e-6f)
            p /= len;
    }

    return planes;
}

bool GpuCullingSystem::isSphereInsideFrustum(const glm::vec3 &center, float radius,
                                             const std::array<glm::vec4, 6> &planes)
{
    for (const auto &plane : planes)
    {
        if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
            return false;
    }
    return true;
}

void GpuCullingSystem::initialize(VkDevice device, uint32_t framesInFlight)
{
    constexpr VkDeviceSize boundsSize = static_cast<VkDeviceSize>(MAX_GPU_CULL_BATCHES) * sizeof(glm::vec4);
    constexpr VkDeviceSize indirectSize = static_cast<VkDeviceSize>(MAX_GPU_CULL_BATCHES) * sizeof(VkDrawIndexedIndirectCommand);

    m_batchBoundsSSBOs.reserve(framesInFlight);
    m_indirectDrawBuffers.reserve(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i)
    {
        m_batchBoundsSSBOs.push_back(core::Buffer::createShared(
            boundsSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        m_indirectDrawBuffers.push_back(core::Buffer::createShared(
            indirectSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU));
    }

    // Descriptor set layout: binding 0 = bounds SSBO, binding 1 = draw-command SSBO
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    }};

    VkDescriptorSetLayoutCreateInfo dslCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslCI.bindingCount = static_cast<uint32_t>(bindings.size());
    dslCI.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &dslCI, nullptr, &m_descriptorSetLayout) != VK_SUCCESS)
    {
        VX_ENGINE_ERROR_STREAM("GpuCullingSystem: failed to create descriptor set layout\n");
        return;
    }

    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, framesInFlight * 2u};
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.maxSets = framesInFlight;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolCI, nullptr, &m_descriptorPool) != VK_SUCCESS)
    {
        VX_ENGINE_ERROR_STREAM("GpuCullingSystem: failed to create descriptor pool\n");
        return;
    }

    m_descriptorSets.resize(framesInFlight, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < framesInFlight; ++i)
    {
        VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_descriptorSetLayout;
        if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSets[i]) != VK_SUCCESS)
        {
            VX_ENGINE_ERROR_STREAM("GpuCullingSystem: failed to allocate descriptor set " << i << "\n");
            return;
        }

        const VkDescriptorBufferInfo boundsBI{m_batchBoundsSSBOs[i]->vk(), 0, VK_WHOLE_SIZE};
        const VkDescriptorBufferInfo cmdBI{m_indirectDrawBuffers[i]->vk(), 0, VK_WHOLE_SIZE};

        const std::array<VkWriteDescriptorSet, 2> writes{{
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSets[i], 0, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &boundsBI, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSets[i], 1, 0, 1,
             VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &cmdBI, nullptr},
        }};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    const VkPushConstantRange pcRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo plCI{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plCI.setLayoutCount = 1;
    plCI.pSetLayouts = &m_descriptorSetLayout;
    plCI.pushConstantRangeCount = 1;
    plCI.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(device, &plCI, nullptr, &m_pipelineLayout) != VK_SUCCESS)
    {
        VX_ENGINE_ERROR_STREAM("GpuCullingSystem: failed to create pipeline layout\n");
        return;
    }

    core::ShaderHandler computeShader;
    computeShader.loadFromFile("resources/shaders/gpu_cull.comp.spv", core::ShaderStage::COMPUTE);
    if (computeShader.getModule() == VK_NULL_HANDLE)
    {
        VX_ENGINE_ERROR_STREAM("GpuCullingSystem: failed to load gpu_cull.comp.spv\n");
        return;
    }

    VkComputePipelineCreateInfo cpCI{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpCI.stage = computeShader.getInfo();
    cpCI.layout = m_pipelineLayout;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpCI, nullptr, &m_pipeline) != VK_SUCCESS)
    {
        VX_ENGINE_ERROR_STREAM("GpuCullingSystem: failed to create compute pipeline\n");
        return;
    }
}

void GpuCullingSystem::cleanup(VkDevice device)
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_descriptorSets.clear();
    m_batchBoundsSSBOs.clear();
    m_indirectDrawBuffers.clear();
}

void GpuCullingSystem::dispatch(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t batchCount,
                                const std::array<glm::vec4, 6> &planes)
{
    // Disabled: GPU cull path causes transient batch disappearance (black frames).
    // CPU-side sphere testing in prepareFrameDataFromScene still runs.
    (void)cmd;
    (void)currentFrame;
    (void)batchCount;
    (void)planes;
    return;

    if (m_pipeline == VK_NULL_HANDLE || batchCount == 0)
        return;

    PushConstants pc{};
    pc.batchCount = batchCount;
    std::copy(planes.begin(), planes.end(), pc.planes);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout,
                            0, 1, &m_descriptorSets[currentFrame], 0, nullptr);
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(PushConstants), &pc);

    const uint32_t groupCount = (batchCount + 63u) / 64u;
    vkCmdDispatch(cmd, groupCount, 1, 1);

    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

core::Buffer *GpuCullingSystem::getBatchBoundsSSBO(uint32_t frameIndex)
{
    return frameIndex < m_batchBoundsSSBOs.size() ? m_batchBoundsSSBOs[frameIndex].get() : nullptr;
}

core::Buffer *GpuCullingSystem::getIndirectDrawBuffer(uint32_t frameIndex)
{
    return frameIndex < m_indirectDrawBuffers.size() ? m_indirectDrawBuffers[frameIndex].get() : nullptr;
}

ELIX_NESTED_NAMESPACE_END