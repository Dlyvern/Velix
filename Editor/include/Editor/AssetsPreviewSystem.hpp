#ifndef ELIX_ASSETS_PREVIEW_SYSTEM
#define ELIX_ASSETS_PREVIEW_SYSTEM

#include "Editor/Project.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Material.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Vertex.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <vector>

#include <backends/imgui_impl_vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

// Class that handle thumbnails of materials/textures
class AssetsPreviewSystem
{
public:
    enum class PreviewKind
    {
        Material,
        Model,
        Texture
    };

    struct RenderPreviewJob
    {
        PreviewKind kind{PreviewKind::Material};
        std::string path;
        engine::Material *material{nullptr};
        engine::GPUMesh *mesh{nullptr};
        glm::mat4 modelTransform{1.0f};
    };

    AssetsPreviewSystem() = default;

    void setProject(Project *project)
    {
        if (m_project != project)
            clearEntries();

        m_project = project;
    }

    void beginFrame()
    {
        ++m_frameIndex;
        m_requestedRenderItemsThisFrame.clear();

        for (auto &[_, e] : m_materialEntries)
            e.requestedThisFrame = false;

        for (auto &[_, e] : m_modelEntries)
            e.requestedThisFrame = false;

        cleanupStaleDescriptors();
    }

    VkDescriptorSet getPlaceholder()
    {
        if (m_placeholder)
            return m_placeholder;

        const auto defaultMaterial = engine::Material::getDefaultMaterial();
        if (!defaultMaterial)
            return VK_NULL_HANDLE;

        const auto &defaultTexture = defaultMaterial->getAlbedoTexture();
        if (!defaultTexture)
            return VK_NULL_HANDLE;

        m_placeholder = ImGui_ImplVulkan_AddTexture(defaultTexture->vkSampler(), defaultTexture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        return m_placeholder;
    }

    bool hasMaterialPreview(const std::string &materialPath) const
    {
        auto it = m_materialEntries.find(materialPath);

        if (it == m_materialEntries.end())
            return false;

        return true;
    }

    std::vector<RenderPreviewJob> captureRequestedRenderJobsForSubmission()
    {
        m_inFlightRenderItems = m_requestedRenderItemsThisFrame;

        std::vector<RenderPreviewJob> jobs;
        jobs.reserve(m_inFlightRenderItems.size());

        for (const auto &item : m_inFlightRenderItems)
        {
            if (item.kind == PreviewKind::Material)
            {
                auto it = m_materialEntries.find(item.path);
                if (it == m_materialEntries.end() || !it->second.material)
                    continue;

                jobs.push_back(RenderPreviewJob{
                    .kind = PreviewKind::Material,
                    .path = item.path,
                    .material = it->second.material.get(),
                    .mesh = nullptr,
                    .modelTransform = glm::mat4(1.0f)});
            }
            else if (item.kind == PreviewKind::Model)
            {
                auto it = m_modelEntries.find(item.path);
                if (it == m_modelEntries.end() || !it->second.material || !it->second.mesh)
                    continue;

                jobs.push_back(RenderPreviewJob{
                    .kind = PreviewKind::Model,
                    .path = item.path,
                    .material = it->second.material.get(),
                    .mesh = it->second.mesh.get(),
                    .modelTransform = it->second.previewTransform});
            }
        }

        return jobs;
    }

    void consumeRenderedJobs(const std::vector<VkImageView> &views, VkSampler sampler)
    {
        const size_t count = std::min(views.size(), m_inFlightRenderItems.size());

        for (size_t i = 0; i < count; ++i)
        {
            const auto &item = m_inFlightRenderItems[i];

            if (item.kind == PreviewKind::Material)
            {
                auto it = m_materialEntries.find(item.path);
                if (it == m_materialEntries.end())
                    continue;

                replaceDescriptor(it->second.imguiDescriptorSet, sampler, views[i]);
                it->second.lastRequestedFrame = m_frameIndex;
            }
            else if (item.kind == PreviewKind::Model)
            {
                auto it = m_modelEntries.find(item.path);
                if (it == m_modelEntries.end())
                    continue;

                replaceDescriptor(it->second.imguiDescriptorSet, sampler, views[i]);
                it->second.lastRequestedFrame = m_frameIndex;
            }
        }

        m_inFlightRenderItems.clear();
    }

    VkDescriptorSet getOrRequestTexturePreview(const std::string &texturePath, engine::Texture::SharedPtr texture = nullptr)
    {
        if (texturePath.empty())
            return getPlaceholder();

        auto &entry = m_textureEntries[texturePath];
        entry.path = texturePath;
        entry.lastRequestedFrame = m_frameIndex;

        if (texture)
            entry.texture = std::move(texture);

        if (!entry.texture && m_project)
        {
            auto it = m_project->cache.texturesByPath.find(texturePath);

            if (it != m_project->cache.texturesByPath.end())
                entry.texture = it->second.gpu;

            if (!entry.texture)
            {
                engine::Texture::SharedPtr loadedTexture = std::make_shared<engine::Texture>();
                if (!loadedTexture->load(texturePath))
                {
                    VX_EDITOR_ERROR_STREAM("Failed to load texture\n");
                    return getPlaceholder();
                }

                m_project->cache.texturesByPath[texturePath].gpu = loadedTexture;
                m_project->cache.texturesByPath[texturePath].path = texturePath;
                m_project->cache.texturesByPath[texturePath].loaded = true;

                entry.texture = loadedTexture;
            }
        }

        if (!entry.texture)
            return getPlaceholder();

        if (!entry.imguiDescriptorSet)
            entry.imguiDescriptorSet = ImGui_ImplVulkan_AddTexture(entry.texture->vkSampler(), entry.texture->vkImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        if (entry.imguiDescriptorSet)
            return entry.imguiDescriptorSet;

        return getPlaceholder();
    }

    VkDescriptorSet getOrRequestMaterialPreview(const std::string &materialPath,
                                                engine::Material::SharedPtr material = nullptr)
    {
        if (materialPath.empty())
            return getPlaceholder();

        auto &entry = m_materialEntries[materialPath];
        entry.path = materialPath;
        entry.kind = PreviewKind::Material;
        entry.lastRequestedFrame = m_frameIndex;

        if (material)
            entry.material = std::move(material);

        if (!entry.requestedThisFrame)
        {
            entry.requestedThisFrame = true;
            m_requestedRenderItemsThisFrame.push_back({PreviewKind::Material, materialPath});
        }

        if (!entry.material && !ensureMaterialLoaded(materialPath, entry.material))
            return getPlaceholder();

        if (entry.imguiDescriptorSet)
            return entry.imguiDescriptorSet;

        return getPlaceholder();
    }

    VkDescriptorSet getOrRequestModelPreview(const std::string &modelPath)
    {
        if (modelPath.empty())
            return getPlaceholder();

        auto &entry = m_modelEntries[modelPath];
        entry.path = modelPath;
        entry.kind = PreviewKind::Model;
        entry.lastRequestedFrame = m_frameIndex;

        if (!entry.requestedThisFrame)
        {
            entry.requestedThisFrame = true;
            m_requestedRenderItemsThisFrame.push_back({PreviewKind::Model, modelPath});
        }

        if ((!entry.mesh || !entry.material) && !ensureModelLoaded(modelPath, entry))
            return getPlaceholder();

        if (entry.imguiDescriptorSet)
            return entry.imguiDescriptorSet;

        return getPlaceholder();
    }

private:
    struct RequestedRenderItem
    {
        PreviewKind kind{PreviewKind::Material};
        std::string path;
    };

    static constexpr uint64_t STALE_DESCRIPTOR_MAX_AGE = 180;

    struct MaterialPreviewEntry
    {
        std::string path;
        PreviewKind kind = PreviewKind::Material;

        engine::Material::SharedPtr material;
        VkDescriptorSet imguiDescriptorSet = VK_NULL_HANDLE;

        bool requestedThisFrame = false;
        bool dirty = true;
        bool ready = false;
        uint64_t lastRequestedFrame = 0;
    };

    struct ModelPreviewEntry
    {
        std::string path;
        PreviewKind kind = PreviewKind::Model;

        engine::GPUMesh::SharedPtr mesh;
        engine::Material::SharedPtr material;
        glm::mat4 previewTransform{1.0f};
        VkDescriptorSet imguiDescriptorSet = VK_NULL_HANDLE;

        bool requestedThisFrame = false;
        uint64_t lastRequestedFrame = 0;
    };

    struct TexturePreviewEntry
    {
        std::string path;
        PreviewKind kind = PreviewKind::Texture;

        engine::Texture::SharedPtr texture;
        VkDescriptorSet imguiDescriptorSet = VK_NULL_HANDLE;

        uint64_t lastRequestedFrame = 0;
    };

    static void removeDescriptor(VkDescriptorSet &descriptorSet)
    {
        if (descriptorSet == VK_NULL_HANDLE)
            return;

        ImGui_ImplVulkan_RemoveTexture(descriptorSet);
        descriptorSet = VK_NULL_HANDLE;
    }

    static void replaceDescriptor(VkDescriptorSet &descriptorSet, VkSampler sampler, VkImageView imageView)
    {
        removeDescriptor(descriptorSet);
        descriptorSet = ImGui_ImplVulkan_AddTexture(sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void clearEntries()
    {
        for (auto &[_, entry] : m_materialEntries)
            removeDescriptor(entry.imguiDescriptorSet);

        for (auto &[_, entry] : m_modelEntries)
            removeDescriptor(entry.imguiDescriptorSet);

        for (auto &[_, entry] : m_textureEntries)
            removeDescriptor(entry.imguiDescriptorSet);

        m_materialEntries.clear();
        m_modelEntries.clear();
        m_textureEntries.clear();
        m_requestedRenderItemsThisFrame.clear();
        m_inFlightRenderItems.clear();
        m_frameIndex = 0;
    }

    bool isDescriptorStale(uint64_t lastRequestedFrame) const
    {
        return m_frameIndex > lastRequestedFrame && (m_frameIndex - lastRequestedFrame) > STALE_DESCRIPTOR_MAX_AGE;
    }

    void cleanupStaleDescriptors()
    {
        for (auto &[_, entry] : m_materialEntries)
        {
            if (entry.requestedThisFrame)
                continue;

            if (entry.imguiDescriptorSet != VK_NULL_HANDLE && isDescriptorStale(entry.lastRequestedFrame))
                removeDescriptor(entry.imguiDescriptorSet);
        }

        for (auto &[_, entry] : m_modelEntries)
        {
            if (entry.requestedThisFrame)
                continue;

            if (entry.imguiDescriptorSet != VK_NULL_HANDLE && isDescriptorStale(entry.lastRequestedFrame))
                removeDescriptor(entry.imguiDescriptorSet);
        }

        for (auto &[_, entry] : m_textureEntries)
        {
            if (entry.imguiDescriptorSet != VK_NULL_HANDLE && isDescriptorStale(entry.lastRequestedFrame))
                removeDescriptor(entry.imguiDescriptorSet);
        }
    }

    engine::Texture::SharedPtr loadOrGetTexture(const std::string &texturePath)
    {
        if (texturePath.empty())
            return nullptr;

        if (m_project)
        {
            auto itTexture = m_project->cache.texturesByPath.find(texturePath);
            if (itTexture != m_project->cache.texturesByPath.end() && itTexture->second.gpu)
                return itTexture->second.gpu;
        }

        auto texture = std::make_shared<engine::Texture>();
        if (!texture->load(texturePath))
            return nullptr;

        if (m_project)
        {
            auto &record = m_project->cache.texturesByPath[texturePath];
            record.path = texturePath;
            record.gpu = texture;
            record.loaded = true;
        }

        return texture;
    }

    static bool tryDecodePreviewVertices(const engine::CPUMesh &source, std::vector<engine::vertex::Vertex3D> &outVertices)
    {
        outVertices.clear();

        if (source.vertexStride == sizeof(engine::vertex::Vertex3D))
        {
            const size_t count = source.vertexData.size() / sizeof(engine::vertex::Vertex3D);
            outVertices.resize(count);
            if (!outVertices.empty())
                std::memcpy(outVertices.data(), source.vertexData.data(), source.vertexData.size());
            return true;
        }

        if (source.vertexStride == sizeof(engine::vertex::VertexSkinned))
        {
            const size_t count = source.vertexData.size() / sizeof(engine::vertex::VertexSkinned);
            if (count == 0)
                return false;

            const auto *skinnedVertices = reinterpret_cast<const engine::vertex::VertexSkinned *>(source.vertexData.data());
            outVertices.reserve(count);

            for (size_t i = 0; i < count; ++i)
            {
                engine::vertex::Vertex3D vertex{};
                vertex.position = skinnedVertices[i].position;
                vertex.textureCoordinates = skinnedVertices[i].textureCoordinates;
                vertex.normal = skinnedVertices[i].normal;
                vertex.tangent = skinnedVertices[i].tangent;
                vertex.bitangent = skinnedVertices[i].bitangent;
                outVertices.push_back(vertex);
            }

            return true;
        }

        return false;
    }

    static glm::mat4 buildNormalizationTransform(const std::vector<engine::vertex::Vertex3D> &vertices)
    {
        if (vertices.empty())
            return glm::mat4(1.0f);

        glm::vec3 minPos(std::numeric_limits<float>::max());
        glm::vec3 maxPos(std::numeric_limits<float>::lowest());

        for (const auto &vertex : vertices)
        {
            minPos = glm::min(minPos, vertex.position);
            maxPos = glm::max(maxPos, vertex.position);
        }

        const glm::vec3 center = (minPos + maxPos) * 0.5f;
        const glm::vec3 size = maxPos - minPos;
        const float maxAxis = std::max({size.x, size.y, size.z, 0.001f});
        const float scale = 1.8f / maxAxis;

        const glm::mat4 translate = glm::translate(glm::mat4(1.0f), -center);
        const glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(scale));

        return scaleMatrix * translate;
    }

    bool ensureMaterialLoaded(const std::string &materialPath, engine::Material::SharedPtr &outMaterial)
    {
        if (outMaterial)
            return true;

        if (!m_project)
            return false;

        auto it = m_project->cache.materialsByPath.find(materialPath);
        if (it != m_project->cache.materialsByPath.end() && it->second.gpu)
        {
            outMaterial = it->second.gpu;
            return true;
        }

        auto materialAsset = engine::AssetsLoader::loadMaterial(materialPath);
        if (!materialAsset.has_value())
        {
            VX_EDITOR_ERROR_STREAM("Failed to load material asset: " << materialPath << '\n');
            return false;
        }

        const auto &materialCPU = materialAsset.value().material;
        auto texture = loadOrGetTexture(materialCPU.albedoTexture);
        auto normalTexture = loadOrGetTexture(materialCPU.normalTexture);
        auto ormTexture = loadOrGetTexture(materialCPU.ormTexture);
        auto emissiveTexture = loadOrGetTexture(materialCPU.emissiveTexture);
        outMaterial = engine::Material::create(texture);

        if (!outMaterial)
            return false;

        outMaterial->setAlbedoTexture(texture);
        outMaterial->setNormalTexture(normalTexture);
        outMaterial->setOrmTexture(ormTexture);
        outMaterial->setEmissiveTexture(emissiveTexture);
        outMaterial->setBaseColorFactor(materialCPU.baseColorFactor);
        outMaterial->setEmissiveFactor(materialCPU.emissiveFactor);
        outMaterial->setMetallic(materialCPU.metallicFactor);
        outMaterial->setRoughness(materialCPU.roughnessFactor);
        outMaterial->setAoStrength(materialCPU.aoStrength);
        outMaterial->setNormalScale(materialCPU.normalScale);
        outMaterial->setAlphaCutoff(materialCPU.alphaCutoff);
        outMaterial->setFlags(materialCPU.flags);
        outMaterial->setUVScale(materialCPU.uvScale);
        outMaterial->setUVOffset(materialCPU.uvOffset);

        auto &record = m_project->cache.materialsByPath[materialPath];
        record.path = materialPath;
        record.cpuData = materialCPU;
        record.gpu = outMaterial;
        record.texture = texture;

        return outMaterial != nullptr;
    }

    bool ensureModelLoaded(const std::string &modelPath, ModelPreviewEntry &entry)
    {
        auto modelAsset = engine::AssetsLoader::loadModel(modelPath);
        if (!modelAsset.has_value())
        {
            VX_EDITOR_ERROR_STREAM("Failed to load model for preview: " << modelPath << '\n');
            return false;
        }

        const auto &model = modelAsset.value();
        if (model.meshes.empty())
            return false;

        std::vector<engine::vertex::Vertex3D> previewVertices;
        engine::CPUMesh previewMesh;
        bool foundPreviewMesh = false;

        for (const auto &sourceMesh : model.meshes)
        {
            if (!tryDecodePreviewVertices(sourceMesh, previewVertices))
                continue;

            if (previewVertices.empty() || sourceMesh.indices.empty())
                continue;

            previewMesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(previewVertices, sourceMesh.indices);
            previewMesh.material = sourceMesh.material;
            foundPreviewMesh = true;
            break;
        }

        if (!foundPreviewMesh)
            return false;

        entry.previewTransform = buildNormalizationTransform(previewVertices);
        entry.mesh = engine::GPUMesh::createFromMesh(previewMesh);

        if (previewMesh.material.albedoTexture.empty())
            entry.material = engine::Material::getDefaultMaterial();
        else
        {
            auto texture = loadOrGetTexture(previewMesh.material.albedoTexture);
            entry.material = engine::Material::create(texture);
        }

        return entry.mesh != nullptr && entry.material != nullptr;
    }

    Project *m_project = nullptr;

    uint64_t m_frameIndex{0};

    std::vector<RequestedRenderItem> m_requestedRenderItemsThisFrame;
    std::vector<RequestedRenderItem> m_inFlightRenderItems;

    std::unordered_map<std::string, MaterialPreviewEntry> m_materialEntries;
    std::unordered_map<std::string, ModelPreviewEntry> m_modelEntries;
    std::unordered_map<std::string, TexturePreviewEntry> m_textureEntries;

    VkDescriptorSet m_placeholder{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_PREVIEW_SYSTEM
