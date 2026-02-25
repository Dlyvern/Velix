#include "Engine/Assets/OBJAssetLoader.hpp"
#include "Engine/Vertex.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <iostream>
#include <unordered_map>
#include <cstdint>
#include <algorithm>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

const std::vector<std::string> OBJAssetLoader::getSupportedFormats() const
{
    return {".obj", ".OBJ"};
}

std::shared_ptr<IAsset> OBJAssetLoader::load(const std::string &filePath)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning;
    std::string error;

    const std::filesystem::path filePathFs(filePath);
    const std::string baseDir = filePathFs.parent_path().string();

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error, filePath.c_str(), baseDir.c_str(), true))
    {
        VX_ENGINE_ERROR_STREAM("Failed to load model: " << filePath << " Error: " << warning << " " << error << '\n');
        return nullptr;
    }

    if (!warning.empty())
        VX_ENGINE_INFO_STREAM("OBJ warning: " << warning << '\n');

    std::vector<CPUMesh> meshes;
    meshes.reserve(shapes.size());

    for (const auto &shape : shapes)
    {
        if (shape.mesh.indices.empty())
            continue;

        std::vector<vertex::Vertex3D> vertices;
        std::vector<uint32_t> indices;
        std::unordered_map<vertex::Vertex3D, uint32_t> uniqueVertices;

        const int materialId = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[0];

        for (const auto &index : shape.mesh.indices)
        {
            vertex::Vertex3D vertex{};

            if (index.vertex_index >= 0)
            {
                vertex.position =
                    {
                        attrib.vertices[3 * index.vertex_index + 0],
                        attrib.vertices[3 * index.vertex_index + 2],
                        attrib.vertices[3 * index.vertex_index + 1],
                    };
            }

            if (index.texcoord_index >= 0)
            {
                vertex.textureCoordinates =
                    {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1],
                    };
            }
            else
                vertex.textureCoordinates = {0.0f, 0.0f};

            if (index.normal_index >= 0)
            {
                vertex.normal =
                    {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 2],
                        attrib.normals[3 * index.normal_index + 1]};
            }
            else
                vertex.normal = {0.0f, 1.0f, 0.0f};

            if (uniqueVertices.count(vertex) == 0)
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }

        if (vertices.empty() || indices.empty())
            continue;

        vertex::generateTangents(vertices, indices);
        CPUMesh mesh = CPUMesh::build<vertex::Vertex3D>(vertices, indices);
        mesh.name = shape.name.empty() ? ("Mesh_" + std::to_string(meshes.size())) : shape.name;

        if (materialId >= 0 && materialId < static_cast<int>(materials.size()))
        {
            if (!materials[materialId].diffuse_texname.empty())
            {
                std::filesystem::path diffusePath = materials[materialId].diffuse_texname;
                if (diffusePath.is_relative())
                    diffusePath = filePathFs.parent_path() / diffusePath;

                mesh.material.name = materials[materialId].name;
                mesh.material.albedoTexture = diffusePath.lexically_normal().string();
            }
        }

        meshes.push_back(std::move(mesh));
    }

    if (meshes.empty())
        return nullptr;

    return std::make_shared<ModelAsset>(meshes);
}

ELIX_NESTED_NAMESPACE_END
