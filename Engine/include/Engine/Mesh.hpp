#ifndef ELIX_MESH_HPP
#define ELIX_MESH_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"
#include "Core/VulkanContext.hpp"

#include "Engine/Caches/Hash.hpp"
#include "Engine/Material.hpp"
#include "Engine/Vertex.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"

#include <memory>
#include <cstdint>

#include <glm/glm.hpp>
#include <cstring>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct CPUMesh
{
    std::vector<uint8_t> vertexData;
    std::vector<uint32_t> indices;

    uint32_t vertexStride;
    uint64_t vertexLayoutHash;

    CPUMaterial material;
    glm::mat4 localTransform{1.0f};

    template <typename VertexT>
    static CPUMesh build(const std::vector<VertexT> &vertices, const std::vector<uint32_t> &indices)
    {
        CPUMesh mesh{};
        mesh.vertexStride = sizeof(VertexT);
        mesh.indices = indices;
        // mesh.material = material;

        auto layout = vertex::VertexTraits<VertexT>::layout();
        mesh.vertexLayoutHash = layout.hash;

        mesh.vertexData.resize(vertices.size() * sizeof(VertexT));
        std::memcpy(mesh.vertexData.data(), vertices.data(), mesh.vertexData.size());

        return mesh;
    }
};

struct GPUMesh
{
    using SharedPtr = std::shared_ptr<GPUMesh>;

    core::Buffer::SharedPtr indexBuffer{nullptr};
    core::Buffer::SharedPtr vertexBuffer{nullptr};
    uint32_t indicesCount{0};
    VkIndexType indexType{VK_INDEX_TYPE_UINT32};

    Material::SharedPtr material{nullptr};

    GPUMesh() = default;

    static std::shared_ptr<GPUMesh> create(const std::vector<uint8_t> &vertexData, std::vector<uint32_t> indices, core::CommandPool::SharedPtr commandPool = nullptr)
    {
        auto gpu = std::make_shared<GPUMesh>();

        VkDeviceSize indexSize = sizeof(indices[0]) * indices.size();
        VkDeviceSize vertexSize = sizeof(vertexData[0]) * vertexData.size();

        auto commandBuffer = core::CommandBuffer::create(core::VulkanContext::getContext()->getGraphicsCommandPool());
        commandBuffer.begin();

        // Vertex
        auto vertexStaging = core::Buffer::create(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
        vertexStaging.upload(vertexData.data(), vertexSize);

        auto vertexGPUBuffer = core::Buffer::createShared(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

        utilities::BufferUtilities::copyBuffer(vertexStaging, *vertexGPUBuffer, commandBuffer, vertexSize);

        // Index
        auto staging = core::Buffer::create(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
        staging.upload(indices.data(), indexSize);

        auto gpuBuffer = core::Buffer::createShared(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

        utilities::BufferUtilities::copyBuffer(staging, *gpuBuffer, commandBuffer, indexSize);

        commandBuffer.end();

        commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
        vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());

        gpu->vertexBuffer = vertexGPUBuffer;
        gpu->indexBuffer = gpuBuffer;

        gpu->indicesCount = static_cast<uint32_t>(indices.size());

        return gpu;
    }

    static std::shared_ptr<GPUMesh> createFromMesh(const CPUMesh &mesh, core::CommandPool::SharedPtr commandPool = nullptr)
    {
        return create(mesh.vertexData, mesh.indices, commandPool);
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MESH_HPP