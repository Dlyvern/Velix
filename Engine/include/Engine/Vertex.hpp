#ifndef VERTEX_HPP
#define VERTEX_HPP

#include "Core/Macros.hpp"

#include "Engine/Caches/Hash.hpp"

#include <vector>
#include <volk.h>
#include <array>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include <glm/gtx/hash.hpp>

//*Formats
// float: VK_FORMAT_R32_SFLOAT
// vec2: VK_FORMAT_R32G32_SFLOAT
// vec3: VK_FORMAT_R32G32B32_SFLOAT
// vec4: VK_FORMAT_R32G32B32A32_SFLOAT
// ivec2: VK_FORMAT_R32G32_SINT, a 2-component vector of 32-bit signed
// integers
// uvec4: VK_FORMAT_R32G32B32A32_UINT, a 4-component vector of 32-bit
// unsigned integers
// double: VK_FORMAT_R64_SFLOAT, a double-precision (64-bit) float

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ELIX_CUSTOM_NAMESPACE_BEGIN(vertex)

class VertexInputAttributeDescription
{
private:
    static std::vector<VkVertexInputAttributeDescription> toVk(const std::vector<VertexInputAttributeDescription> &input)
    {
        std::vector<VkVertexInputAttributeDescription> attributes;

        for (const auto &in : input)
        {
            VkVertexInputAttributeDescription description{
                .location = in.m_location,
                .binding = in.m_binding,
                .format = in.m_format,
                .offset = in.m_offset};

            attributes.push_back(std::move(description));
        }

        return attributes;
    }

    void setName(const std::string &name)
    {
        m_name = name;
    }

    void setOffset(uint32_t offset)
    {
        m_offset = offset;
    }

    void setLocation(uint32_t location)
    {
        m_location = location;
    }

    void setBinding(uint32_t binding)
    {
        m_binding = binding;
    }

    void setFormat(VkFormat format)
    {
        m_format = format;
    }

    const std::string &getName() const
    {
        return m_name;
    }

    uint32_t getOffset() const
    {
        return m_offset;
    }

    uint32_t getLocation() const
    {
        return m_location;
    }

    uint32_t getBinding() const
    {
        return m_binding;
    }

    VkFormat getFormat() const
    {
        return m_format;
    }

public:
    std::string m_name;
    uint32_t m_offset;
    uint32_t m_location;
    uint32_t m_binding;
    VkFormat m_format;
};

inline VkVertexInputBindingDescription getBindingDescription(uint32_t stride)
{
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

struct VertexSkinned
{
    glm::vec3 position{1.0f};
    glm::vec2 textureCoordinates{1.0f};
    glm::vec3 normal{1.0f};
    glm::vec3 tangent{1.0f};
    glm::vec3 bitangent{1.0f};
    glm::ivec4 boneIds{-1};
    glm::vec4 weights{1.0f};

    VertexSkinned() = default;

    VertexSkinned(glm::vec3 position, glm::vec2 textureCoordinates, glm::vec3 normal,
                  glm::vec3 tangent, glm::vec3 bitangent, glm::ivec4 boneIds, glm::vec4 weights)
    {
        this->position = position;
        this->textureCoordinates = textureCoordinates;
        this->normal = normal;
        this->tangent = tangent;
        this->bitangent = bitangent;
        this->boneIds = boneIds;
        this->weights = weights;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
    {
        std::vector<VkVertexInputAttributeDescription> attributes(7);

        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSkinned, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VertexSkinned, textureCoordinates)};
        attributes[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSkinned, normal)};
        attributes[3] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSkinned, tangent)};
        attributes[4] = {4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexSkinned, bitangent)};
        attributes[5] = {5, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(VertexSkinned, boneIds)};
        attributes[6] = {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexSkinned, weights)};

        return attributes;
    }
};

struct Vertex2D
{
    glm::vec3 position{1.0f};
    glm::vec2 textureCoordinates{1.0f};

    bool operator==(const Vertex2D &other) const
    {
        return position == other.position &&
               textureCoordinates == other.textureCoordinates;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
    {
        std::vector<VkVertexInputAttributeDescription> attributes(2);
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, textureCoordinates)};
        return attributes;
    }
};

struct Vertex3D
{
    glm::vec3 position{1.0f};
    glm::vec2 textureCoordinates{1.0f};
    glm::vec3 normal{1.0f};

    Vertex3D() = default;

    Vertex3D(const glm::vec3 &pos, const glm::vec2 &texCoords, const glm::vec3 &nor) : position(pos), textureCoordinates(texCoords), normal(nor)
    {
    }

    bool operator==(const Vertex3D &other) const
    {
        return position == other.position &&
               normal == other.normal &&
               textureCoordinates == other.textureCoordinates;
    }

    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
    {
        std::vector<VkVertexInputAttributeDescription> attributes(3);

        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, textureCoordinates)};
        attributes[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)};

        return attributes;
    }
};

struct VertexLayout
{
    VkVertexInputBindingDescription binding;
    std::vector<VkVertexInputAttributeDescription> attributes;
    uint64_t hash;
};

template <typename VertexT>
struct VertexTraits;

template <>
struct VertexTraits<vertex::Vertex2D>
{
    static constexpr uint32_t stride = sizeof(vertex::Vertex2D);

    static VertexLayout layout()
    {
        VertexLayout layout{};
        layout.binding = vertex::getBindingDescription(stride);
        layout.attributes = vertex::Vertex2D::getAttributeDescriptions();

        hashing::hash(layout.hash, layout.binding.binding);
        hashing::hash(layout.hash, layout.binding.inputRate);
        hashing::hash(layout.hash, layout.binding.stride);

        for (auto &a : layout.attributes)
        {
            hashing::hash(layout.hash, a.binding);
            hashing::hash(layout.hash, a.location);
            hashing::hash(layout.hash, a.format);
            hashing::hash(layout.hash, a.offset);
        }

        return layout;
    }
};

template <>
struct VertexTraits<vertex::Vertex3D>
{
    static constexpr uint32_t stride = sizeof(vertex::Vertex3D);

    static VertexLayout layout()
    {
        VertexLayout layout{};
        layout.binding = vertex::getBindingDescription(stride);
        layout.attributes = vertex::Vertex3D::getAttributeDescriptions();

        hashing::hash(layout.hash, layout.binding.binding);
        hashing::hash(layout.hash, layout.binding.inputRate);
        hashing::hash(layout.hash, layout.binding.stride);

        for (auto &a : layout.attributes)
        {
            hashing::hash(layout.hash, a.binding);
            hashing::hash(layout.hash, a.location);
            hashing::hash(layout.hash, a.format);
            hashing::hash(layout.hash, a.offset);
        }

        return layout;
    }
};

template <>
struct VertexTraits<vertex::VertexSkinned>
{
    static constexpr uint32_t stride = sizeof(vertex::VertexSkinned);

    static VertexLayout layout()
    {
        VertexLayout layout{};
        layout.binding = vertex::getBindingDescription(stride);
        layout.attributes = vertex::VertexSkinned::getAttributeDescriptions();

        hashing::hash(layout.hash, layout.binding.binding);
        hashing::hash(layout.hash, layout.binding.inputRate);
        hashing::hash(layout.hash, layout.binding.stride);

        for (auto &a : layout.attributes)
        {
            hashing::hash(layout.hash, a.binding);
            hashing::hash(layout.hash, a.location);
            hashing::hash(layout.hash, a.format);
            hashing::hash(layout.hash, a.offset);
        }

        return layout;
    }
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

namespace std
{
    template <>
    struct hash<elix::engine::vertex::Vertex3D>
    {
        size_t operator()(elix::engine::vertex::Vertex3D const &vertex) const
        {
            return ((hash<glm::vec3>()(vertex.position) ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.textureCoordinates) << 1);
        }
    };
}
#endif // VERTEX_HPP