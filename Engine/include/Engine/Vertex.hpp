#ifndef VERTEX_HPP
#define VERTEX_HPP

#include "Core/Macros.hpp"


#include <vector>
#include <volk.h>
#include <array>

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include <glm/gtx/hash.hpp>

//*Formats
//float: VK_FORMAT_R32_SFLOAT
//vec2: VK_FORMAT_R32G32_SFLOAT
//vec3: VK_FORMAT_R32G32B32_SFLOAT
//vec4: VK_FORMAT_R32G32B32A32_SFLOAT
//ivec2: VK_FORMAT_R32G32_SINT, a 2-component vector of 32-bit signed
// integers
//uvec4: VK_FORMAT_R32G32B32A32_UINT, a 4-component vector of 32-bit
// unsigned integers
//double: VK_FORMAT_R64_SFLOAT, a double-precision (64-bit) float

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct Vertex2D
{
    glm::vec3 position;
    glm::vec2 textureCoordinates;

    bool operator==(const Vertex2D& other) const 
    {
        return position == other.position &&
               textureCoordinates == other.textureCoordinates;
    }

    static VkVertexInputBindingDescription getBindingDescription() 
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex2D);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 2> attributes{};
        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex2D, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex2D, textureCoordinates)};
        return attributes;
    }
};

struct Vertex3D
{
    glm::vec3 position{1.0f};
    glm::vec2 textureCoordinates{1.0f};
    glm::vec3 normal;

    Vertex3D() = default;

    Vertex3D(const glm::vec3& pos, const glm::vec2& texCoords, const glm::vec3& nor) :
    position(pos), textureCoordinates(texCoords), normal(nor)
    {

    }

    bool operator==(const Vertex3D& other) const 
    {
        return position == other.position &&
               normal == other.normal &&
               textureCoordinates == other.textureCoordinates;
    }

    static VkVertexInputBindingDescription getBindingDescription() 
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex3D);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> attributes{};

        attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, position)};
        attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex3D, textureCoordinates)};
        attributes[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal)};

        return attributes;
    }
};

ELIX_NESTED_NAMESPACE_END

namespace std
{
    template<> struct hash<elix::engine::Vertex3D> 
    {
        size_t operator()(elix::engine::Vertex3D const& vertex) const 
        {
            return ((hash<glm::vec3>()(vertex.position) ^ (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.textureCoordinates) << 1);
        }
    };
} 
#endif //VERTEX_HPP