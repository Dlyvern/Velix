#ifndef ELIX_MESH_HPP
#define ELIX_MESH_HPP

#include "Core/Macros.hpp"
#include "Engine/Vertex.hpp"
#include "Core/Buffer.hpp"
#include <cstdint>
#include "Engine/Hash.hpp"

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Mesh3D
{
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    Mesh3D(const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices)
    {
        this->vertices = vertices;
        this->indices = indices;
    }
};

struct Mesh2D
{
    std::vector<Vertex2D> vertices;
    std::vector<uint32_t> indices;

    Mesh2D(const std::vector<Vertex2D>& vertices, const std::vector<uint32_t>& indices)
    {
        this->vertices = vertices;
        this->indices = indices;
    }
};

struct GPUMesh
{
    core::Buffer::SharedPtr indexBuffer{nullptr};
    core::Buffer::SharedPtr vertexBuffer{nullptr};
    uint32_t indicesCount{0};
    VkIndexType indexType{VK_INDEX_TYPE_UINT32};
    uint64_t vertexLayoutHash{0};

    VkVertexInputBindingDescription bindingDescription;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

    GPUMesh() = default;

    static std::shared_ptr<GPUMesh> createFromMesh(const Mesh3D& mesh, VkQueue queue, core::CommandPool::SharedPtr commandPool)
    {
        auto gpuMesh = std::make_shared<GPUMesh>();
        
        VkDeviceSize indexSize = sizeof(mesh.indices[0]) * mesh.indices.size();
        VkDeviceSize vertexSize = sizeof(mesh.vertices[0]) * mesh.vertices.size();

        gpuMesh->indexBuffer = core::Buffer::createCopied(mesh.indices.data(), indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, commandPool, queue);
        gpuMesh->vertexBuffer = core::Buffer::createCopied(mesh.vertices.data(), vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, commandPool, queue);

        gpuMesh->indicesCount = static_cast<uint32_t>(mesh.indices.size());
        gpuMesh->indexType = VK_INDEX_TYPE_UINT32;

        auto bindingDescription = engine::Vertex3D::getBindingDescription();
        auto attributeDescription = engine::Vertex3D::getAttributeDescriptions();

        gpuMesh->bindingDescription = bindingDescription;

        hashing::hash(gpuMesh->vertexLayoutHash, (bindingDescription.binding));
        hashing::hash(gpuMesh->vertexLayoutHash, (bindingDescription.inputRate));
        hashing::hash(gpuMesh->vertexLayoutHash, (bindingDescription.stride));

        for(const auto& b : attributeDescription)
        {
            hashing::hash(gpuMesh->vertexLayoutHash, (b.binding));
            hashing::hash(gpuMesh->vertexLayoutHash, (b.format));
            hashing::hash(gpuMesh->vertexLayoutHash, (b.location));
            hashing::hash(gpuMesh->vertexLayoutHash, (b.offset));
            gpuMesh->attributeDescriptions.push_back(b);
        }

        return gpuMesh;
    }

    static std::shared_ptr<GPUMesh> createFromMesh(const Mesh2D& mesh, VkQueue queue, core::CommandPool::SharedPtr commandPool)
    {
        auto gpuMesh = std::make_shared<GPUMesh>();

        VkDeviceSize indexSize = sizeof(mesh.indices[0]) * mesh.indices.size();
        VkDeviceSize vertexSize = sizeof(mesh.vertices[0]) * mesh.vertices.size();

        gpuMesh->indexBuffer = core::Buffer::createCopied(mesh.indices.data(), indexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, commandPool, queue);
        gpuMesh->vertexBuffer = core::Buffer::createCopied(mesh.vertices.data(), vertexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, commandPool, queue);
        
        gpuMesh->indicesCount = static_cast<uint32_t>(mesh.indices.size());
        gpuMesh->indexType = VK_INDEX_TYPE_UINT32;

        auto bindingDescription = engine::Vertex2D::getBindingDescription();
        auto attributeDescription = engine::Vertex2D::getAttributeDescriptions();

        hashing::hash(gpuMesh->vertexLayoutHash, (bindingDescription.binding));
        hashing::hash(gpuMesh->vertexLayoutHash, (bindingDescription.inputRate));
        hashing::hash(gpuMesh->vertexLayoutHash, (bindingDescription.stride));

        for(const auto& b : attributeDescription)
        {
            hashing::hash(gpuMesh->vertexLayoutHash, (b.binding));
            hashing::hash(gpuMesh->vertexLayoutHash, (b.format));
            hashing::hash(gpuMesh->vertexLayoutHash, (b.location));
            hashing::hash(gpuMesh->vertexLayoutHash, (b.offset));
        }

        return gpuMesh;
    }
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_MESH_HPP