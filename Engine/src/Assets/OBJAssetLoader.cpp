#include "Engine/Assets/OBJAssetLoader.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <iostream>
#include <unordered_map>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

const std::vector<std::string> OBJAssetLoader::getSupportedFormats() const
{
    return {".obj", ".OBJ"};
}

std::shared_ptr<IAsset> OBJAssetLoader::load(const std::string& filePath)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string error;

    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    if(!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, filePath.c_str()))
    {
        std::cerr << "Failed to load model: " << filePath << " Error: " << warning << " " << error << std::endl;
        return nullptr;
    }

    std::unordered_map<Vertex3D, uint32_t> uniqueVertices{};

    for(const auto& shape : shapes)
    {
        for(const auto& index : shape.mesh.indices)
        {
            Vertex3D vertex;
            
            vertex.position = 
            {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 2],
                attrib.vertices[3 * index.vertex_index + 1],
            };
            vertex.textureCoordinates = 
            {
                attrib.texcoords[2 * index.texcoord_index + 0],
                1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
            };

            vertex.normal = {1.0f, 1.0f, 1.0f};

            if(uniqueVertices.count(vertex) == 0)
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            // vertices.push_back(vertex);
            indices.push_back(uniqueVertices[vertex]);
        }
    }

    auto modelAsset = std::make_shared<ModelAsset>(Mesh3D{vertices, indices});

    return modelAsset;
}

ELIX_NESTED_NAMESPACE_END