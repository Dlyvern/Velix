#include "Engine/Assets/OBJAssetLoader.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <iostream>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

const std::vector<std::string> OBJAssetLoader::getSupportedFormats() const
{
    return {".obj", ".OBJ"};
}

std::shared_ptr<IAsset> OBJAssetLoader::load(const std::string &filePath)
{
    return nullptr;
    // tinyobj::attrib_t attrib;
    // std::vector<tinyobj::shape_t> shapes;
    // std::vector<tinyobj::material_t> materials;
    // std::string warning;
    // std::string error;

    // std::vector<vertex::Vertex3D> vertices;
    // std::vector<uint32_t> indices;
    // std::string baseDir = filePath.substr(0, filePath.find_last_of("/\\") + 1);

    // if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, filePath.c_str(), baseDir.c_str(), true))
    // {
    //     std::cerr << "Failed to load model: " << filePath << " Error: " << warning << " " << error << std::endl;
    //     return nullptr;
    // }

    // std::vector<Mesh3D> meshes;

    // if (materials.empty())
    //     std::cerr << "No materials " << std::endl;
    // else
    //     std::cout << "Has materials" << std::endl;

    // for (const auto &shape : shapes)
    // {
    //     std::vector<vertex::Vertex3D> vertices;
    //     std::vector<uint32_t> indices;
    //     std::unordered_map<vertex::Vertex3D, uint32_t> uniqueVertices{};

    //     int materialId = -1;
    //     if (!shape.mesh.material_ids.empty())
    //         materialId = shape.mesh.material_ids[0];

    //     for (const auto &index : shape.mesh.indices)
    //     {
    //         vertex::Vertex3D vertex{};

    //         vertex.position =
    //             {
    //                 attrib.vertices[3 * index.vertex_index + 0],
    //                 attrib.vertices[3 * index.vertex_index + 2],
    //                 attrib.vertices[3 * index.vertex_index + 1],
    //             };

    //         if (index.texcoord_index >= 0)
    //         {
    //             vertex.textureCoordinates =
    //                 {
    //                     attrib.texcoords[2 * index.texcoord_index + 0],
    //                     1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
    //                 };
    //         }
    //         else
    //             vertex.textureCoordinates = {0.0f, 0.0f};

    //         if (index.normal_index >= 0)
    //         {
    //             vertex.normal =
    //                 {
    //                     attrib.normals[3 * index.normal_index + 0],
    //                     attrib.normals[3 * index.normal_index + 2],
    //                     attrib.normals[3 * index.normal_index + 1]};
    //         }
    //         else
    //             vertex.normal = {0.0f, 1.0f, 0.0f};

    //         if (uniqueVertices.count(vertex) == 0)
    //         {
    //             uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
    //             vertices.push_back(vertex);
    //         }

    //         indices.push_back(uniqueVertices[vertex]);
    //     }

    //     if (!vertices.empty())
    //     {
    //         // shape.name
    //         Mesh3D mesh{vertices, indices};

    //         if (materialId >= 0 && materialId < materials.size())
    //         {
    //             if (!materials[materialId].diffuse_texname.empty())
    //             {
    //                 mesh.material.name = materials[materialId].name;
    //                 mesh.material.albedoTexture = materials[materialId].diffuse_texname;

    //                 std::replace(mesh.material.albedoTexture.begin(), mesh.material.albedoTexture.end(), '\\', '/');

    //                 std::string actualPath = "./resources/" + mesh.material.albedoTexture;

    //                 mesh.material.albedoTexture = actualPath;

    //                 std::cout << "Found texture: " << actualPath << std::endl;
    //             }
    //         }

    //         meshes.push_back(mesh);
    //     }
    // }

    // auto modelAsset = std::make_shared<ModelAsset>(meshes);

    // return modelAsset;
}

ELIX_NESTED_NAMESPACE_END