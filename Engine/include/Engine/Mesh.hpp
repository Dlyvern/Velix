#ifndef ELIX_MESH_HPP
#define ELIX_MESH_HPP

#include "Core/Macros.hpp"
#include "Engine/Vertex.hpp"
#include "Core/Buffer.hpp"
#include <cstdint>
#include "Engine/Hash.hpp"

#include "Engine/Material.hpp"

#include "Core/VulkanContext.hpp"
#include <memory>

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

        gpu->vertexBuffer = core::Buffer::createCopied(
            vertexData.data(),
            vertexData.size(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU,
            commandPool);

        gpu->indexBuffer = core::Buffer::createCopied(
            indices.data(),
            indexSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU,
            commandPool);

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