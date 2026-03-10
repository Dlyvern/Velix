#include "Engine/RayTracing/RayTracedMesh.hpp"

#include "Core/VulkanContext.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(rayTracing)

namespace
{
    bool submitAndWait(core::CommandBuffer::SharedPtr commandBuffer, VkQueue queue)
    {
        if (!commandBuffer || queue == VK_NULL_HANDLE)
            return false;

        auto context = core::VulkanContext::getContext();
        if (!context)
            return false;

        VkFenceCreateInfo fenceCreateInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(context->getDevice(), &fenceCreateInfo, nullptr, &fence) != VK_SUCCESS)
            return false;

        const bool submitted = commandBuffer->submit(queue, {}, {}, {}, fence);
        const bool waited = submitted && vkWaitForFences(context->getDevice(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;

        vkDestroyFence(context->getDevice(), fence, nullptr);
        return submitted && waited;
    }
} // namespace

RayTracedMesh::SharedPtr RayTracedMesh::createFromMesh(const GPUMesh &mesh, core::CommandPool::SharedPtr commandPool)
{
    auto rayTracedMesh = std::make_shared<RayTracedMesh>();
    if (!rayTracedMesh->uploadFromMesh(mesh, commandPool))
        return nullptr;

    return rayTracedMesh;
}

bool RayTracedMesh::uploadFromMesh(const GPUMesh &mesh, core::CommandPool::SharedPtr commandPool)
{
    auto context = core::VulkanContext::getContext();
    if (!context || !context->hasBufferDeviceAddressSupport() || !context->hasAccelerationStructureSupport())
        return false;

    if (vertexBuffer && indexBuffer)
        return true;

    if (!mesh.vertexBuffer || !mesh.indexBuffer)
        return false;

    auto pool = commandPool ? commandPool : context->getGraphicsCommandPool();
    if (!pool)
        return false;

    auto commandBuffer = core::CommandBuffer::createShared(*pool);
    if (!commandBuffer->begin())
        return false;

    auto newVertexBuffer = core::Buffer::createShared(
        mesh.vertexBuffer->getSize(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);

    auto newIndexBuffer = core::Buffer::createShared(
        mesh.indexBuffer->getSize(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        core::memory::MemoryUsage::GPU_ONLY);

    utilities::BufferUtilities::copyBuffer(*mesh.vertexBuffer, *newVertexBuffer, *commandBuffer, mesh.vertexBuffer->getSize());
    utilities::BufferUtilities::copyBuffer(*mesh.indexBuffer, *newIndexBuffer, *commandBuffer, mesh.indexBuffer->getSize());

    if (!commandBuffer->end())
        return false;

    if (!submitAndWait(commandBuffer, context->getGraphicsQueue()))
        return false;

    vertexBuffer = std::move(newVertexBuffer);
    indexBuffer = std::move(newIndexBuffer);
    return true;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
