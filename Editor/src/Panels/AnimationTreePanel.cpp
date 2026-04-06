#include "Editor/Panels/AnimationTreePanel.hpp"
#include "Editor/Project.hpp"
#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Caches/Hash.hpp"
#include "Engine/Material.hpp"
#include "Engine/Vertex.hpp"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_node_editor.h"
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

namespace ed = ax::NodeEditor;

ELIX_NESTED_NAMESPACE_BEGIN(editor)

namespace
{
    constexpr float kPreviewMinDistance = 0.35f;
    constexpr float kPreviewMaxDistance = 40.0f;

    struct PreviewCandidateInfo
    {
        size_t slot{0u};
        engine::CPUMesh previewMesh{};
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        float score{-1.0f};
        bool skinned{false};
    };

    bool looksLikeWindowsAbsolutePath(const std::string &path)
    {
        return path.size() >= 3u &&
               std::isalpha(static_cast<unsigned char>(path[0])) &&
               path[1] == ':' &&
               (path[2] == '\\' || path[2] == '/');
    }

    std::string resolvePreviewAssetPath(const std::string &path, const std::string &referenceAssetPath)
    {
        if (path.empty())
            return {};

        const std::filesystem::path asPath(path);
        if (asPath.is_absolute() || looksLikeWindowsAbsolutePath(path))
            return asPath.lexically_normal().string();

        if (!referenceAssetPath.empty())
            return (std::filesystem::path(referenceAssetPath).parent_path() / asPath).lexically_normal().string();

        return asPath.lexically_normal().string();
    }

    engine::Material::SharedPtr createPreviewMaterial(const engine::CPUMaterial &materialCPU,
                                                      const std::string &referenceAssetPath)
    {
        engine::Texture::SharedPtr albedoTexture{nullptr};
        if (!materialCPU.albedoTexture.empty())
            albedoTexture = engine::AssetsLoader::loadTextureGPU(resolvePreviewAssetPath(materialCPU.albedoTexture, referenceAssetPath));
        auto material = engine::Material::create(albedoTexture);
        if (!material)
            return engine::Material::getDefaultMaterial();

        if (!materialCPU.normalTexture.empty())
            material->setNormalTexture(engine::AssetsLoader::loadTextureGPU(resolvePreviewAssetPath(materialCPU.normalTexture, referenceAssetPath),
                                                                            VK_FORMAT_R8G8B8A8_UNORM));
        if (!materialCPU.ormTexture.empty())
            material->setOrmTexture(engine::AssetsLoader::loadTextureGPU(resolvePreviewAssetPath(materialCPU.ormTexture, referenceAssetPath),
                                                                         VK_FORMAT_R8G8B8A8_UNORM));
        if (!materialCPU.emissiveTexture.empty())
            material->setEmissiveTexture(engine::AssetsLoader::loadTextureGPU(resolvePreviewAssetPath(materialCPU.emissiveTexture, referenceAssetPath)));

        material->setBaseColorFactor(materialCPU.baseColorFactor);
        material->setEmissiveFactor(materialCPU.emissiveFactor);
        material->setMetallic(materialCPU.metallicFactor);
        material->setRoughness(materialCPU.roughnessFactor);
        material->setAoStrength(materialCPU.aoStrength);
        material->setNormalScale(materialCPU.normalScale);
        material->setAlphaCutoff(materialCPU.alphaCutoff);
        material->setUVScale(materialCPU.uvScale);
        material->setUVOffset(materialCPU.uvOffset);
        material->setUVRotation(materialCPU.uvRotation);
        material->setFlags(materialCPU.flags);
        material->setIor(materialCPU.ior);
        material->setDomain(materialCPU.domain);
        material->setDecalBlendMode(materialCPU.decalBlendMode);
        return material;
    }

    bool buildPreviewMesh(const engine::CPUMesh &source,
                          engine::CPUMesh &outMesh,
                          glm::vec3 &outMinPos,
                          glm::vec3 &outMaxPos)
    {
        outMesh = {};
        outMinPos = glm::vec3(std::numeric_limits<float>::max());
        outMaxPos = glm::vec3(std::numeric_limits<float>::lowest());

        if (source.vertexStride == sizeof(engine::vertex::VertexSkinned))
        {
            const size_t vertexCount = source.vertexData.size() / sizeof(engine::vertex::VertexSkinned);
            if (vertexCount == 0u || source.indices.empty())
                return false;

            const auto *sourceVertices = reinterpret_cast<const engine::vertex::VertexSkinned *>(source.vertexData.data());
            std::vector<engine::vertex::Vertex3D> vertices;
            vertices.reserve(vertexCount);

            for (size_t i = 0; i < vertexCount; ++i)
            {
                engine::vertex::Vertex3D vertex{};
                vertex.position = sourceVertices[i].position;
                vertex.textureCoordinates = sourceVertices[i].textureCoordinates;
                vertex.normal = sourceVertices[i].normal;
                vertex.tangent = sourceVertices[i].tangent;
                vertex.bitangent = sourceVertices[i].bitangent;
                outMinPos = glm::min(outMinPos, vertex.position);
                outMaxPos = glm::max(outMaxPos, vertex.position);
                vertices.push_back(vertex);
            }

            outMesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(vertices, source.indices);
        }
        else if (source.vertexStride == sizeof(engine::vertex::Vertex3D))
        {
            const size_t vertexCount = source.vertexData.size() / sizeof(engine::vertex::Vertex3D);
            if (vertexCount == 0u || source.indices.empty())
                return false;

            const auto *sourceVertices = reinterpret_cast<const engine::vertex::Vertex3D *>(source.vertexData.data());
            std::vector<engine::vertex::Vertex3D> vertices;
            vertices.reserve(vertexCount);

            for (size_t i = 0; i < vertexCount; ++i)
            {
                engine::vertex::Vertex3D vertex{};
                vertex.position = sourceVertices[i].position;
                vertex.textureCoordinates = sourceVertices[i].textureCoordinates;
                vertex.normal = sourceVertices[i].normal;
                vertex.tangent = sourceVertices[i].tangent;
                vertex.bitangent = sourceVertices[i].bitangent;
                outMinPos = glm::min(outMinPos, vertex.position);
                outMaxPos = glm::max(outMaxPos, vertex.position);
                vertices.push_back(vertex);
            }

            outMesh = engine::CPUMesh::build<engine::vertex::Vertex3D>(vertices, source.indices);
        }
        else
        {
            return false;
        }

        outMesh.name = source.name;
        outMesh.material = source.material;
        outMesh.localTransform = source.localTransform;
        outMesh.attachedBoneId = source.attachedBoneId;
        return true;
    }

    bool isSkinnedPreviewSource(const engine::CPUMesh &mesh)
    {
        return mesh.vertexStride == sizeof(engine::vertex::VertexSkinned) ||
               mesh.vertexLayoutHash == engine::vertex::VertexTraits<engine::vertex::VertexSkinned>::layout().hash;
    }

    size_t computeAnimationTreeHash(const engine::AnimationTree &tree)
    {
        using engine::hashing::hashCombine;

        size_t seed = 0u;
        hashCombine(seed, tree.graphVersion);
        hashCombine(seed, tree.nextNodeId);
        hashCombine(seed, tree.rootMachineNodeId);

        for (const auto &node : tree.graphNodes)
        {
            hashCombine(seed, node.id);
            hashCombine(seed, static_cast<uint32_t>(node.type));
            hashCombine(seed, node.parentMachineNodeId);
            hashCombine(seed, node.name);
            hashCombine(seed, node.editorPosition.x);
            hashCombine(seed, node.editorPosition.y);
            hashCombine(seed, node.entryNodeId);
            hashCombine(seed, node.animationAssetPath);
            hashCombine(seed, node.clipIndex);
            hashCombine(seed, node.loop);
            hashCombine(seed, node.speed);
            hashCombine(seed, node.blendParameterName);

            for (int childNodeId : node.childNodeIds)
                hashCombine(seed, childNodeId);

            for (const auto &sample : node.blendSamples)
            {
                hashCombine(seed, sample.animationAssetPath);
                hashCombine(seed, sample.clipIndex);
                hashCombine(seed, sample.position);
            }

            for (const auto &transition : node.transitions)
            {
                hashCombine(seed, transition.fromNodeId);
                hashCombine(seed, transition.toNodeId);
                hashCombine(seed, transition.blendDuration);
                hashCombine(seed, transition.hasExitTime);
                hashCombine(seed, transition.exitTime);

                for (const auto &condition : transition.conditions)
                {
                    hashCombine(seed, static_cast<uint32_t>(condition.type));
                    hashCombine(seed, condition.parameterName);
                    hashCombine(seed, condition.floatThreshold);
                    hashCombine(seed, condition.intValue);
                }
            }
        }

        for (const auto &parameter : tree.parameters)
        {
            hashCombine(seed, parameter.name);
            hashCombine(seed, static_cast<uint32_t>(parameter.type));
            hashCombine(seed, parameter.floatDefault);
            hashCombine(seed, parameter.boolDefault);
            hashCombine(seed, parameter.intDefault);
        }
        return seed;
    }

    const char *animationTreeNodeTypeLabel(engine::AnimationTreeNode::Type type)
    {
        switch (type)
        {
        case engine::AnimationTreeNode::Type::StateMachine:
            return "State Machine";
        case engine::AnimationTreeNode::Type::ClipState:
            return "Clip State";
        case engine::AnimationTreeNode::Type::BlendSpace1D:
            return "BlendSpace1D";
        }

        return "Unknown";
    }

    bool ensurePreviewMeshSourceLoaded(AnimationTreePanel::AnimPreviewContext &preview)
    {
        if (!preview.skeletalMesh)
            return false;

        if (!preview.skeletalMesh->getMeshes().empty())
            return true;

        auto &handle = preview.skeletalMesh->getModelHandle();
        if (handle.empty())
            return false;

        if (handle.state() == engine::AssetState::Unloaded)
            engine::AssetManager::getInstance().requestLoad(handle);

        if (handle.ready() && preview.skeletalMesh->getMeshes().empty())
            preview.skeletalMesh->onModelLoaded();

        return !preview.skeletalMesh->getMeshes().empty();
    }

    const engine::AnimationTreeParameter *findAnimationTreeParameter(const engine::AnimationTree &tree,
                                                                     const std::string &name)
    {
        for (const auto &param : tree.parameters)
        {
            if (param.name == name)
                return &param;
        }

        return nullptr;
    }

    void updateSkinnedPreviewVertices(const engine::CPUMesh &sourceMesh,
                                      const std::vector<glm::mat4> &finalBones,
                                      std::vector<engine::vertex::Vertex3D> &ioVertices)
    {
        const size_t vertexCount = sourceMesh.vertexData.size() / sizeof(engine::vertex::VertexSkinned);
        if (vertexCount == 0u)
        {
            ioVertices.clear();
            return;
        }

        const auto *sourceVertices = reinterpret_cast<const engine::vertex::VertexSkinned *>(sourceMesh.vertexData.data());
        ioVertices.resize(vertexCount);

        for (size_t i = 0; i < vertexCount; ++i)
        {
            const auto &sourceVertex = sourceVertices[i];

            glm::mat4 boneTransform(0.0f);
            bool hasBone = false;
            for (int boneInfluence = 0; boneInfluence < 4; ++boneInfluence)
            {
                const int boneId = sourceVertex.boneIds[boneInfluence];
                const float weight = sourceVertex.weights[boneInfluence];
                if (boneId < 0 || weight <= 0.0f || static_cast<size_t>(boneId) >= finalBones.size())
                    continue;

                boneTransform += finalBones[static_cast<size_t>(boneId)] * weight;
                hasBone = true;
            }

            if (!hasBone)
                boneTransform = glm::mat4(1.0f);

            engine::vertex::Vertex3D vertex{};
            vertex.position = glm::vec3(boneTransform * glm::vec4(sourceVertex.position, 1.0f));
            vertex.textureCoordinates = sourceVertex.textureCoordinates;
            vertex.normal = sourceVertex.normal;
            vertex.tangent = sourceVertex.tangent;
            vertex.bitangent = sourceVertex.bitangent;
            ioVertices[i] = vertex;
        }
    }

    glm::mat4 buildPreviewNormalizationTransform(const glm::vec3 &minPos, const glm::vec3 &maxPos)
    {
        const glm::vec3 center = (minPos + maxPos) * 0.5f;
        const glm::vec3 size = maxPos - minPos;
        const float maxAxis = std::max({size.x, size.y, size.z, 0.001f});
        const float scale = 0.30f / maxAxis;
        return glm::scale(glm::mat4(1.0f), glm::vec3(scale)) *
               glm::translate(glm::mat4(1.0f), -center);
    }

    void expandBoundsByTransformedAabb(const glm::vec3 &localMin,
                                       const glm::vec3 &localMax,
                                       const glm::mat4 &transform,
                                       glm::vec3 &ioMin,
                                       glm::vec3 &ioMax)
    {
        const glm::vec3 corners[8] = {
            {localMin.x, localMin.y, localMin.z},
            {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z},
            {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z},
            {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z},
            {localMax.x, localMax.y, localMax.z},
        };

        for (const glm::vec3 &corner : corners)
        {
            const glm::vec3 transformed = glm::vec3(transform * glm::vec4(corner, 1.0f));
            ioMin = glm::min(ioMin, transformed);
            ioMax = glm::max(ioMax, transformed);
        }
    }

    bool buildPreviewCandidate(const engine::CPUMesh &sourceMesh,
                               size_t slot,
                               PreviewCandidateInfo &outCandidate)
    {
        engine::CPUMesh previewMesh{};
        glm::vec3 localMin{};
        glm::vec3 localMax{};
        if (!buildPreviewMesh(sourceMesh, previewMesh, localMin, localMax))
            return false;

        glm::vec3 transformedMin(std::numeric_limits<float>::max());
        glm::vec3 transformedMax(std::numeric_limits<float>::lowest());
        expandBoundsByTransformedAabb(localMin, localMax, previewMesh.localTransform, transformedMin, transformedMax);

        if (transformedMin.x > transformedMax.x ||
            transformedMin.y > transformedMax.y ||
            transformedMin.z > transformedMax.z)
            return false;

        outCandidate.slot = slot;
        outCandidate.previewMesh = std::move(previewMesh);
        outCandidate.boundsMin = transformedMin;
        outCandidate.boundsMax = transformedMax;
        outCandidate.score = glm::length(transformedMax - transformedMin);
        outCandidate.skinned = isSkinnedPreviewSource(sourceMesh);
        return true;
    }

} // namespace

AnimationTreePanel::~AnimationTreePanel()
{
    invalidateLivePreviewDescriptor();

    for (auto &[key, ui] : m_uiStates)
    {
        if (ui.nodeEditorContext)
        {
            ed::DestroyEditor(ui.nodeEditorContext);
            ui.nodeEditorContext = nullptr;
        }
    }
}

void AnimationTreePanel::setPreviewPass(AnimationTreePreviewPass *pass)
{
    if (m_previewPass == pass)
        return;

    m_previewPass = pass;
    invalidateLivePreviewDescriptor();
}

void AnimationTreePanel::setPreviewDescriptorSet(VkDescriptorSet ds)
{
    if (m_sharedPreviewDescriptorSet == ds)
        return;

    m_sharedPreviewDescriptorSet = ds;
    invalidateLivePreviewDescriptor();
}

bool AnimationTreePanel::hasActivePreview() const
{
    for (const auto &[_, ui] : m_uiStates)
    {
        const auto &preview = ui.preview;
        if (preview.active && preview.animator && preview.skeletalMesh)
            return true;
    }

    return false;
}

void AnimationTreePanel::invalidateLivePreviewDescriptor()
{
    if (m_livePreviewDescriptorSet != VK_NULL_HANDLE)
        ImGui_ImplVulkan_RemoveTexture(m_livePreviewDescriptorSet);

    m_livePreviewDescriptorSet = VK_NULL_HANDLE;
    m_livePreviewImageView = VK_NULL_HANDLE;
    m_livePreviewSampler = VK_NULL_HANDLE;
}

void AnimationTreePanel::refreshLivePreviewDescriptor()
{
    if (!m_previewPass)
        return;

    const VkImageView imageView = m_previewPass->getOutputImageView();
    const VkSampler sampler = m_previewPass->getOutputSampler();
    if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
        return;

    if (m_livePreviewDescriptorSet != VK_NULL_HANDLE &&
        m_livePreviewImageView == imageView &&
        m_livePreviewSampler == sampler)
        return;

    invalidateLivePreviewDescriptor();
    m_livePreviewDescriptorSet = ImGui_ImplVulkan_AddTexture(
        sampler,
        imageView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_livePreviewImageView = imageView;
    m_livePreviewSampler = sampler;
}

void AnimationTreePanel::openTree(const std::filesystem::path &path,
                                  engine::AnimatorComponent *animator,
                                  engine::SkeletalMeshComponent *skeletalMesh)
{
    const std::string key = path.lexically_normal().string();
    const bool hasPreviewSource = animator && skeletalMesh;

    for (auto &editor : m_openEditors)
    {
        if (editor.path == path)
        {
            editor.open = true;
            auto it = m_uiStates.find(key);
            if (it != m_uiStates.end())
            {
                auto &preview = it->second.preview;
                if (hasPreviewSource)
                {
                    preview.animator = animator;
                    preview.skeletalMesh = skeletalMesh;
                    preview.active = true;
                    preview.runtimeReady = false;
                    preview.playing = true;
                    preview.playbackSpeed = 1.0f;
                    preview.syncedTreeHash = 0u;
                    preview.previewMeshes.clear(); // will be rebuilt lazily in update()
                    preview.modelMatrix = glm::mat4(1.0f);
                    preview.isFirstActivation = true;
                }
                else
                {
                    preview = AnimPreviewContext{};
                }
            }
            return;
        }
    }

    OpenTreeEditor editor{};
    editor.path = path;
    editor.open = true;
    m_openEditors.push_back(std::move(editor));

    if (m_uiStates.find(key) == m_uiStates.end())
    {
        AnimTreeUIState ui{};
        auto loaded = engine::AssetsLoader::loadAnimationTree(key);
        if (loaded.has_value())
            ui.tree = std::move(loaded.value());
        else
            ui.tree.name = path.stem().string();

        ui.tree.ensureGraph();
        ui.currentMachineNodeId = ui.tree.rootMachineNodeId;

        if (hasPreviewSource)
        {
            ui.preview.animator = animator;
            ui.preview.skeletalMesh = skeletalMesh;
            ui.preview.active = true;
            ui.preview.runtimeReady = false;
            ui.preview.playing = true;
            ui.preview.playbackSpeed = 1.0f;
            ui.preview.syncedTreeHash = 0u;
            ui.preview.isFirstActivation = true;
            // GPU meshes are created lazily in update() to avoid calling Vulkan during ImGui render phase
        }

        m_uiStates[key] = std::move(ui);
    }
}

void AnimationTreePanel::closeTree(const std::filesystem::path &path)
{
    m_openEditors.erase(
        std::remove_if(m_openEditors.begin(), m_openEditors.end(),
                       [&path](const OpenTreeEditor &e)
                       { return e.path == path; }),
        m_openEditors.end());

    const std::string key = path.lexically_normal().string();
    auto it = m_uiStates.find(key);
    if (it != m_uiStates.end())
    {
        if (it->second.nodeEditorContext)
            ed::DestroyEditor(it->second.nodeEditorContext);
        m_uiStates.erase(it);
    }
}

void AnimationTreePanel::renameOpenTree(const std::filesystem::path &oldPath, const std::filesystem::path &newPath)
{
    if (oldPath.empty() || newPath.empty())
        return;

    const std::filesystem::path normalizedOldPath = oldPath.lexically_normal();
    const std::filesystem::path normalizedNewPath = newPath.lexically_normal();

    for (auto &editor : m_openEditors)
    {
        if (editor.path == normalizedOldPath)
            editor.path = normalizedNewPath;
    }

    const std::string oldKey = normalizedOldPath.string();
    const std::string newKey = normalizedNewPath.string();
    if (oldKey == newKey)
        return;

    auto stateIt = m_uiStates.find(oldKey);
    if (stateIt == m_uiStates.end())
        return;

    auto stateNode = m_uiStates.extract(stateIt);
    stateNode.key() = newKey;
    stateNode.mapped().tree.assetPath = newKey;
    m_uiStates.insert(std::move(stateNode));
}

void AnimationTreePanel::draw()
{
    m_hasKeyboardFocus = false;

    std::vector<std::filesystem::path> closedEditors;
    for (auto &editor : m_openEditors)
    {
        drawSingleEditor(editor);
        if (!editor.open)
            closedEditors.push_back(editor.path);
    }

    for (const auto &path : closedEditors)
        closeTree(path);
}

bool AnimationTreePanel::hasOpenEditors() const
{
    return std::any_of(m_openEditors.begin(), m_openEditors.end(), [](const OpenTreeEditor &editor)
                       { return editor.open; });
}

void AnimationTreePanel::drawSingleEditor(OpenTreeEditor &editor)
{
    const std::string key = editor.path.lexically_normal().string();
    const std::string stem = editor.path.stem().string();

    auto it = m_uiStates.find(key);
    if (it == m_uiStates.end())
        return;
    AnimTreeUIState &ui = it->second;

    const std::string windowTitle = (ui.dirty ? "* " : "") + stem + " [Animation Tree]###AnimTreeEditor:" + key;

    ui.tree.ensureGraph();
    if (!ui.tree.findNode(ui.currentMachineNodeId))
        ui.currentMachineNodeId = ui.tree.rootMachineNodeId;
    if (!ui.tree.findNode(ui.selectedNodeId))
        ui.selectedNodeId = -1;
    if (!ui.tree.findNode(ui.selectedTransitionMachineNodeId))
    {
        ui.selectedTransitionIndex = -1;
        ui.selectedTransitionMachineNodeId = -1;
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGuiWindowClass windowClass;
    windowClass.TabItemFlagsOverrideSet = ImGuiTabItemFlags_Trailing;
    ImGui::SetNextWindowClass(&windowClass);
    if (m_requestedFocusWindowId != 0 && ImHashStr(windowTitle.c_str()) == m_requestedFocusWindowId)
        ImGui::SetNextWindowFocus();
    if (m_centerDockId != 0)
    {
        ImGui::DockBuilderDockWindow(windowTitle.c_str(), m_centerDockId);
        ImGui::SetNextWindowDockID(m_centerDockId, ImGuiCond_Always);
    }
    if (!ImGui::Begin(windowTitle.c_str(), &editor.open, flags))
    {
        m_hasKeyboardFocus = m_hasKeyboardFocus ||
                             ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        ImGui::End();
        return;
    }

    m_hasKeyboardFocus = m_hasKeyboardFocus ||
                         ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Toolbar
    if (ImGui::Button("Save"))
    {
        if (m_saveTreeFunction && m_saveTreeFunction(editor.path, ui.tree))
        {
            ui.dirty = false;
            if (m_notificationManager)
                m_notificationManager->showSuccess("Animation tree saved");
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", key.c_str());

    ImGui::Separator();

    // Layout: left panel (parameters + preview) | right (node graph + inspector)
    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float sideWidth = glm::clamp(totalWidth * 0.28f, 260.0f, 400.0f);
    const float totalHeight = ImGui::GetContentRegionAvail().y;
    const float inspectorHeight = 180.0f;
    const float graphHeight = totalHeight - inspectorHeight - ImGui::GetStyle().ItemSpacing.y;

    ImGui::BeginChild("##AnimTreeParams", ImVec2(sideWidth, totalHeight), true);
    drawParametersPanel(ui);
    drawPreviewPane(ui);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##AnimTreeRight", ImVec2(0.0f, totalHeight), false);
    {
        const std::vector<int> machinePath = ui.tree.buildNodePath(ui.currentMachineNodeId);
        if (!machinePath.empty())
        {
            ImGui::TextDisabled("Graph");
            ImGui::SameLine();
            for (size_t pathIndex = 0; pathIndex < machinePath.size(); ++pathIndex)
            {
                const auto *machineNode = ui.tree.findNode(machinePath[pathIndex]);
                if (!machineNode || !machineNode->isStateMachine())
                    continue;

                if (pathIndex > 0)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(">");
                    ImGui::SameLine();
                }

                if (ImGui::SmallButton(machineNode->name.c_str()))
                {
                    ui.currentMachineNodeId = machineNode->id;
                    ui.selectedNodeId = -1;
                    ui.selectedTransitionIndex = -1;
                    ui.selectedTransitionMachineNodeId = -1;
                }
                if (pathIndex + 1 < machinePath.size())
                    ImGui::SameLine();
            }

            ImGui::Separator();
        }

        ImGui::BeginChild("##AnimTreeGraph", ImVec2(0.0f, graphHeight), true);
        drawNodeGraph(ui, editor.path);
        ImGui::EndChild();

        ImGui::BeginChild("##AnimTreeInspector", ImVec2(0.0f, inspectorHeight), true);
        drawTransitionInspector(ui);
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::End();
}

void AnimationTreePanel::drawParametersPanel(AnimTreeUIState &ui)
{
    ImGui::TextDisabled("Parameters");
    ImGui::Separator();

    if (ImGui::Button("+ Add", ImVec2(-1.0f, 0.0f)))
        ui.addParamPopupOpen = true;

    if (ui.addParamPopupOpen)
        ImGui::OpenPopup("AddParamPopup");

    if (ImGui::BeginPopup("AddParamPopup"))
    {
        ImGui::InputText("Name##newparam", ui.newParamName, sizeof(ui.newParamName));
        ImGui::Combo("Type##newparamtype", &ui.newParamType, "Float\0Bool\0Int\0Trigger\0");

        if (ImGui::Button("Add") && ui.newParamName[0] != '\0')
        {
            engine::AnimationTreeParameter param{};
            param.name = ui.newParamName;
            param.type = static_cast<engine::AnimationTreeParameter::Type>(ui.newParamType);
            ui.tree.parameters.push_back(param);
            ui.dirty = true;
            ui.addParamPopupOpen = false;
            ui.newParamName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ui.addParamPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();

    int removeIdx = -1;
    for (int i = 0; i < static_cast<int>(ui.tree.parameters.size()); ++i)
    {
        auto &param = ui.tree.parameters[static_cast<size_t>(i)];
        ImGui::PushID(i);

        const char *typeTag = "F";
        switch (param.type)
        {
        case engine::AnimationTreeParameter::Type::Float:
            typeTag = "F";
            break;
        case engine::AnimationTreeParameter::Type::Bool:
            typeTag = "B";
            break;
        case engine::AnimationTreeParameter::Type::Int:
            typeTag = "I";
            break;
        case engine::AnimationTreeParameter::Type::Trigger:
            typeTag = "T";
            break;
        }

        ImGui::TextDisabled("[%s]", typeTag);
        ImGui::SameLine();
        ImGui::TextUnformatted(param.name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            removeIdx = i;

        // Editable default value
        switch (param.type)
        {
        case engine::AnimationTreeParameter::Type::Float:
            if (ImGui::DragFloat("##fv", &param.floatDefault, 0.01f))
                ui.dirty = true;
            break;
        case engine::AnimationTreeParameter::Type::Bool:
            if (ImGui::Checkbox("##bv", &param.boolDefault))
                ui.dirty = true;
            break;
        case engine::AnimationTreeParameter::Type::Int:
            if (ImGui::InputInt("##iv", &param.intDefault))
                ui.dirty = true;
            break;
        case engine::AnimationTreeParameter::Type::Trigger:
            ImGui::TextDisabled("(trigger)");
            break;
        }

        ImGui::PopID();
        ImGui::Separator();
    }

    if (removeIdx >= 0)
    {
        ui.tree.parameters.erase(ui.tree.parameters.begin() + removeIdx);
        ui.dirty = true;
    }
}

void AnimationTreePanel::drawNodeGraph(AnimTreeUIState &ui, const std::filesystem::path &path)
{
    const std::string key = path.lexically_normal().string();
    ui.tree.ensureGraph();

    engine::AnimationTreeNode *currentMachine = ui.tree.findNode(ui.currentMachineNodeId);
    if (!currentMachine || !currentMachine->isStateMachine())
    {
        ImGui::TextDisabled("Select a state machine to edit.");
        return;
    }
    const int currentMachineId = currentMachine->id;

    if (!ui.initialized)
    {
        ed::Config config;
        config.SettingsFile = nullptr;
        ui.nodeEditorContext = ed::CreateEditor(&config);
        ui.initialized = true;

        if (ui.nodeEditorContext)
        {
            ed::SetCurrentEditor(ui.nodeEditorContext);
            for (const auto &node : ui.tree.graphNodes)
            {
                ed::SetNodePosition(ed::NodeId(graphNodeId(node.id)),
                                    ImVec2(node.editorPosition.x, node.editorPosition.y));
            }
            ed::SetCurrentEditor(nullptr);
        }
    }

    ed::SetCurrentEditor(ui.nodeEditorContext);
    ed::Begin(("AnimTree##" + key).c_str(), ImVec2(0.0f, 0.0f));

    ed::SetNodePosition(ed::NodeId(entryNodeId(currentMachineId)), ImVec2(20.0f, 80.0f));
    ed::SetNodePosition(ed::NodeId(anyNodeId(currentMachineId)), ImVec2(20.0f, 220.0f));

    // Entry node for the current machine
    ed::BeginNode(ed::NodeId(entryNodeId(currentMachineId)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
    ImGui::Text("Entry");
    ImGui::PopStyleColor();
    ed::BeginPin(ed::PinId(entryOutPin(currentMachineId)), ed::PinKind::Output);
    ImGui::Text("Start >");
    ed::EndPin();
    ed::EndNode();

    // Any-state node for the current machine
    ed::BeginNode(ed::NodeId(anyNodeId(currentMachineId)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.2f, 1.0f));
    ImGui::Text("Any");
    ImGui::PopStyleColor();
    ed::BeginPin(ed::PinId(anyOutPin(currentMachineId)), ed::PinKind::Output);
    ImGui::Text("From >");
    ed::EndPin();
    ed::EndNode();

    auto isChildOfCurrentMachine = [&currentMachine](int nodeId)
    {
        return std::find(currentMachine->childNodeIds.begin(),
                         currentMachine->childNodeIds.end(),
                         nodeId) != currentMachine->childNodeIds.end();
    };

    for (int childNodeId : currentMachine->childNodeIds)
    {
        engine::AnimationTreeNode *node = ui.tree.findNode(childNodeId);
        if (!node)
            continue;

        const bool isEntry = currentMachine->entryNodeId == node->id;
        ed::BeginNode(ed::NodeId(graphNodeId(node->id)));

        if (isEntry)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
        ImGui::Text("%s", node->name.c_str());
        if (isEntry)
            ImGui::PopStyleColor();

        ImGui::TextDisabled("%s", animationTreeNodeTypeLabel(node->type));

        if (node->type == engine::AnimationTreeNode::Type::StateMachine)
        {
            ImGui::TextDisabled("%zu children", node->childNodeIds.size());
            ImGui::TextDisabled("Double-click to enter");
        }
        else if (node->type == engine::AnimationTreeNode::Type::ClipState)
        {
            const std::string clipText = node->animationAssetPath.empty()
                                             ? "Drop .anim here"
                                             : std::filesystem::path(node->animationAssetPath).stem().string();
            if (node->animationAssetPath.empty())
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));

            ImGui::Selectable((clipText + "##clipdrop" + std::to_string(node->id)).c_str(), false,
                              ImGuiSelectableFlags_None, ImVec2(160.0f, 0.0f));

            if (node->animationAssetPath.empty())
                ImGui::PopStyleColor();

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    const std::string dropped(static_cast<const char *>(payload->Data),
                                              static_cast<size_t>(payload->DataSize) - 1u);
                    if (dropped.find(".anim.elixasset") != std::string::npos)
                    {
                        node->animationAssetPath = dropped;
                        ui.dirty = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        else if (node->type == engine::AnimationTreeNode::Type::BlendSpace1D)
        {
            const std::string paramLabel = node->blendParameterName.empty()
                                               ? "<float parameter>"
                                               : node->blendParameterName;
            ImGui::TextDisabled("Param: %s", paramLabel.c_str());
            ImGui::TextDisabled("%zu samples", node->blendSamples.size());
        }

        ed::BeginPin(ed::PinId(graphNodeInPin(node->id)), ed::PinKind::Input);
        ImGui::Text("->");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(ed::PinId(graphNodeOutPin(node->id)), ed::PinKind::Output);
        ImGui::Text("o-");
        ed::EndPin();

        ed::EndNode();
    }

    if (currentMachine->entryNodeId >= 0 && isChildOfCurrentMachine(currentMachine->entryNodeId))
    {
        ed::Link(ed::LinkId(transitionLinkId(currentMachineId, -1)),
                 ed::PinId(entryOutPin(currentMachineId)),
                 ed::PinId(graphNodeInPin(currentMachine->entryNodeId)),
                 ImVec4(0.2f, 0.9f, 0.3f, 1.0f), 2.0f);
    }

    for (int transitionIndex = 0; transitionIndex < static_cast<int>(currentMachine->transitions.size()); ++transitionIndex)
    {
        const auto &transition = currentMachine->transitions[static_cast<size_t>(transitionIndex)];
        if (!isChildOfCurrentMachine(transition.toNodeId))
            continue;
        if (transition.fromNodeId != engine::AnimationTree::ANY_NODE_ID &&
            !isChildOfCurrentMachine(transition.fromNodeId))
            continue;

        const int fromOut = (transition.fromNodeId == engine::AnimationTree::ANY_NODE_ID)
                                ? anyOutPin(currentMachineId)
                                : graphNodeOutPin(transition.fromNodeId);
        const int toIn = graphNodeInPin(transition.toNodeId);
        const bool selected = (ui.selectedTransitionMachineNodeId == currentMachineId &&
                               ui.selectedTransitionIndex == transitionIndex);
        const ImVec4 color = selected ? ImVec4(1.0f, 0.7f, 0.1f, 1.0f) : ImVec4(0.7f, 0.7f, 0.9f, 1.0f);
        ed::Link(ed::LinkId(transitionLinkId(currentMachineId, transitionIndex)),
                 ed::PinId(fromOut),
                 ed::PinId(toIn),
                 color,
                 selected ? 2.5f : 1.5f);
    }

    auto pinToOutputNode = [&](ed::PinId pin) -> int
    {
        const int raw = static_cast<int>(pin.Get());
        if (raw == entryOutPin(currentMachineId))
            return -2; // entry output pin
        if (raw == anyOutPin(currentMachineId))
            return -3; // any-state output pin
        if (raw >= 4000000 && ((raw - 4000000) % 2) == 1)
        {
            const int nodeId = (raw - 4000001) / 2;
            return isChildOfCurrentMachine(nodeId) ? nodeId : -1;
        }
        return -1;
    };
    auto pinToInputNode = [&](ed::PinId pin) -> int
    {
        const int raw = static_cast<int>(pin.Get());
        if (raw >= 4000000 && ((raw - 4000000) % 2) == 0)
        {
            const int nodeId = (raw - 4000000) / 2;
            return isChildOfCurrentMachine(nodeId) ? nodeId : -1;
        }
        return -1;
    };

    if (ed::BeginCreate(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), 2.0f))
    {
        ed::PinId startPinId, endPinId;
        if (ed::QueryNewLink(&startPinId, &endPinId))
        {
            if (startPinId && endPinId)
            {
                int fromNodeId = pinToOutputNode(startPinId);
                int toNodeId = pinToInputNode(endPinId);

                if (fromNodeId == -1 || toNodeId == -1)
                {
                    fromNodeId = pinToOutputNode(endPinId);
                    toNodeId = pinToInputNode(startPinId);
                }

                const bool toValid = isChildOfCurrentMachine(toNodeId);
                const bool fromValid = (fromNodeId == -2 || fromNodeId == -3) ||
                                       (isChildOfCurrentMachine(fromNodeId) && fromNodeId != toNodeId);

                if (!toValid || !fromValid)
                {
                    ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
                }
                else if (ed::AcceptNewItem(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), 2.5f))
                {
                    if (fromNodeId == -2)
                    {
                        currentMachine->entryNodeId = toNodeId;
                    }
                    else
                    {
                        engine::AnimationGraphTransition transition{};
                        transition.fromNodeId = (fromNodeId == -3) ? engine::AnimationTree::ANY_NODE_ID : fromNodeId;
                        transition.toNodeId = toNodeId;
                        currentMachine->transitions.push_back(transition);
                        ui.selectedTransitionMachineNodeId = currentMachineId;
                        ui.selectedTransitionIndex = static_cast<int>(currentMachine->transitions.size()) - 1;
                    }
                    ui.dirty = true;
                }
            }
        }

        // Dragging a pin into empty space — just reject gracefully
        ed::PinId unusedPin;
        if (ed::QueryNewNode(&unusedPin))
            ed::RejectNewItem(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), 1.5f);
    }
    ed::EndCreate();

    if (ed::BeginDelete())
    {
        ed::LinkId deletedLink;
        while (ed::QueryDeletedLink(&deletedLink))
        {
            const int linkRaw = static_cast<int>(deletedLink.Get());

            if (linkRaw == transitionLinkId(currentMachineId, -1))
            {
                ed::RejectDeletedItem();
            }
            else if (linkRaw >= transitionLinkId(currentMachineId, 0) &&
                     linkRaw < transitionLinkId(currentMachineId, 0) + 10000)
            {
                if (ed::AcceptDeletedItem())
                {
                    const int transitionIndex = linkRaw - transitionLinkId(currentMachineId, 0);
                    if (transitionIndex >= 0 &&
                        transitionIndex < static_cast<int>(currentMachine->transitions.size()))
                    {
                        currentMachine->transitions.erase(currentMachine->transitions.begin() + transitionIndex);
                        if (ui.selectedTransitionMachineNodeId == currentMachineId &&
                            ui.selectedTransitionIndex == transitionIndex)
                        {
                            ui.selectedTransitionIndex = -1;
                            ui.selectedTransitionMachineNodeId = -1;
                        }
                        else if (ui.selectedTransitionMachineNodeId == currentMachineId &&
                                 ui.selectedTransitionIndex > transitionIndex)
                            --ui.selectedTransitionIndex;
                        ui.dirty = true;
                    }
                }
            }
            else
            {
                ed::RejectDeletedItem();
            }
        }

        ed::NodeId deletedNode;
        while (ed::QueryDeletedNode(&deletedNode))
        {
            const int nodeRaw = static_cast<int>(deletedNode.Get());
            if (nodeRaw >= 3000000)
            {
                const int nodeId = nodeRaw - 3000000;
                if (ed::AcceptDeletedItem() && isChildOfCurrentMachine(nodeId))
                {
                    std::vector<int> removedNodeIds;
                    std::function<void(int)> collectDescendants = [&](int childId)
                    {
                        removedNodeIds.push_back(childId);
                        if (const auto *childNode = ui.tree.findNode(childId))
                        {
                            for (int nestedChildId : childNode->childNodeIds)
                                collectDescendants(nestedChildId);
                        }
                    };
                    collectDescendants(nodeId);

                    auto isRemoved = [&removedNodeIds](int id)
                    {
                        return std::find(removedNodeIds.begin(), removedNodeIds.end(), id) != removedNodeIds.end();
                    };

                    currentMachine->transitions.erase(
                        std::remove_if(currentMachine->transitions.begin(),
                                       currentMachine->transitions.end(),
                                       [&isRemoved](const engine::AnimationGraphTransition &transition)
                                       {
                                           return isRemoved(transition.toNodeId) ||
                                                  (transition.fromNodeId != engine::AnimationTree::ANY_NODE_ID &&
                                                   isRemoved(transition.fromNodeId));
                                       }),
                        currentMachine->transitions.end());

                    currentMachine->childNodeIds.erase(
                        std::remove_if(currentMachine->childNodeIds.begin(),
                                       currentMachine->childNodeIds.end(),
                                       [&isRemoved](int childId)
                                       { return isRemoved(childId); }),
                        currentMachine->childNodeIds.end());

                    if (isRemoved(currentMachine->entryNodeId))
                        currentMachine->entryNodeId =
                            currentMachine->childNodeIds.empty() ? -1 : currentMachine->childNodeIds.front();

                    ui.tree.graphNodes.erase(
                        std::remove_if(ui.tree.graphNodes.begin(),
                                       ui.tree.graphNodes.end(),
                                       [&isRemoved](const engine::AnimationTreeNode &node)
                                       { return isRemoved(node.id); }),
                        ui.tree.graphNodes.end());

                    if (isRemoved(ui.selectedNodeId))
                        ui.selectedNodeId = -1;
                    if (ui.selectedTransitionMachineNodeId == currentMachineId)
                    {
                        if (engine::AnimationTreeNode *machineAfterDelete = ui.tree.findNode(currentMachineId);
                            !machineAfterDelete ||
                            ui.selectedTransitionIndex >= static_cast<int>(machineAfterDelete->transitions.size()))
                        {
                            ui.selectedTransitionIndex = -1;
                            ui.selectedTransitionMachineNodeId = -1;
                        }
                    }

                    ui.dirty = true;
                }
            }
        }
    }
    ed::EndDelete();

    if (ed::GetSelectedObjectCount() > 0)
    {
        ed::LinkId selectedLink;
        if (ed::GetSelectedLinks(&selectedLink, 1) == 1)
        {
            const int raw = static_cast<int>(selectedLink.Get());
            if (raw >= transitionLinkId(currentMachineId, 0) &&
                raw < transitionLinkId(currentMachineId, 0) + 10000)
            {
                ui.selectedTransitionMachineNodeId = currentMachineId;
                ui.selectedTransitionIndex = raw - transitionLinkId(currentMachineId, 0);
                ui.selectedNodeId = -1;
            }
        }
        else
        {
            ed::NodeId selectedNode;
            if (ed::GetSelectedNodes(&selectedNode, 1) == 1)
            {
                const int raw = static_cast<int>(selectedNode.Get());
                if (raw >= 3000000)
                {
                    ui.selectedNodeId = raw - 3000000;
                    ui.selectedTransitionIndex = -1;
                    ui.selectedTransitionMachineNodeId = -1;
                }
            }
        }
    }
    else
    {
        ui.selectedTransitionIndex = -1;
        ui.selectedTransitionMachineNodeId = -1;
    }

    if (ed::ShowBackgroundContextMenu())
        ImGui::OpenPopup("AnimTreeBGMenu");

    ed::Suspend();
    if (ImGui::BeginPopup("AnimTreeBGMenu"))
    {
        if (ImGui::MenuItem("Add Node"))
        {
            ui.addNodePopupOpen = true;
            ui.newNodeType = 1;
            ui.newNodeName[0] = '\0';
            ui.newNodeClipPath[0] = '\0';
        }
        ImGui::EndPopup();
    }
    ed::Resume();

    ed::Suspend();
    if (ui.addNodePopupOpen)
        ImGui::OpenPopup("AddNodePopup##animtree");

    if (ImGui::BeginPopupModal("AddNodePopup##animtree", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Combo("Type", &ui.newNodeType, "State Machine\0Clip State\0BlendSpace1D\0");
        ImGui::InputText("Name", ui.newNodeName, sizeof(ui.newNodeName));

        if (ui.newNodeType == 1)
        {
            ImGui::InputText("Clip path", ui.newNodeClipPath, sizeof(ui.newNodeClipPath));
            ImGui::TextDisabled("(or drag .anim.elixasset after creating)");

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    const std::string dropped(static_cast<const char *>(payload->Data), payload->DataSize - 1);
                    std::strncpy(ui.newNodeClipPath, dropped.c_str(), sizeof(ui.newNodeClipPath) - 1);
                }
                ImGui::EndDragDropTarget();
            }
        }

        if (ImGui::Button("Add") && ui.newNodeName[0] != '\0')
        {
            const auto nodeType = static_cast<engine::AnimationTreeNode::Type>(ui.newNodeType);
            const glm::vec2 newPosition(300.0f + static_cast<float>(currentMachine->childNodeIds.size()) * 220.0f, 80.0f);
            const int nodeId = ui.tree.addGraphNode(nodeType, ui.newNodeName, currentMachine->id, newPosition);
            if (engine::AnimationTreeNode *node = ui.tree.findNode(nodeId))
            {
                if (nodeType == engine::AnimationTreeNode::Type::ClipState)
                    node->animationAssetPath = ui.newNodeClipPath;
                else if (nodeType == engine::AnimationTreeNode::Type::BlendSpace1D)
                {
                    for (const auto &parameter : ui.tree.parameters)
                    {
                        if (parameter.type == engine::AnimationTreeParameter::Type::Float)
                        {
                            node->blendParameterName = parameter.name;
                            break;
                        }
                    }
                }
            }
            if (ui.nodeEditorContext)
            {
                ed::SetCurrentEditor(ui.nodeEditorContext);
                ed::SetNodePosition(ed::NodeId(graphNodeId(nodeId)), ImVec2(newPosition.x, newPosition.y));
                ed::SetCurrentEditor(nullptr);
            }

            ui.selectedNodeId = nodeId;
            ui.dirty = true;
            ui.addNodePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ui.addNodePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ed::Resume();

    ed::NodeId ctxNode;
    if (ed::ShowNodeContextMenu(&ctxNode))
    {
        const int nodeRaw = static_cast<int>(ctxNode.Get());
        if (nodeRaw >= 3000000)
        {
            const int nodeId = nodeRaw - 3000000;
            if (isChildOfCurrentMachine(nodeId))
            {
                ImGui::OpenPopup(("NodeCtxMenu##" + std::to_string(nodeId)).c_str());
            }
        }
    }

    for (int childNodeId : currentMachine->childNodeIds)
    {
        ed::Suspend();
        drawNodeContextMenu(ui, childNodeId);
        ed::Resume();
    }

    const int doubleClickedNodeRaw = static_cast<int>(ed::GetDoubleClickedNode().Get());
    if (doubleClickedNodeRaw >= 3000000)
    {
        const int nodeId = doubleClickedNodeRaw - 3000000;
        if (engine::AnimationTreeNode *node = ui.tree.findNode(nodeId);
            node && node->type == engine::AnimationTreeNode::Type::StateMachine)
        {
            ui.currentMachineNodeId = node->id;
            ui.selectedNodeId = -1;
            ui.selectedTransitionIndex = -1;
            ui.selectedTransitionMachineNodeId = -1;
        }
    }

    for (int childNodeId : currentMachine->childNodeIds)
    {
        if (engine::AnimationTreeNode *node = ui.tree.findNode(childNodeId))
        {
            const ImVec2 nodePosition = ed::GetNodePosition(ed::NodeId(graphNodeId(node->id)));
            const glm::vec2 updatedPosition(nodePosition.x, nodePosition.y);
            if (node->editorPosition != updatedPosition)
            {
                node->editorPosition = updatedPosition;
                ui.dirty = true;
            }
        }
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void AnimationTreePanel::drawNodeContextMenu(AnimTreeUIState &ui, int nodeId)
{
    engine::AnimationTreeNode *node = ui.tree.findNode(nodeId);
    if (!node)
        return;

    const std::string popupId = "NodeCtxMenu##" + std::to_string(nodeId);
    if (!ImGui::BeginPopup(popupId.c_str()))
        return;

    engine::AnimationTreeNode *parentMachine = ui.tree.findNode(node->parentMachineNodeId);

    if (parentMachine && parentMachine->isStateMachine() && ImGui::MenuItem("Set as Entry"))
    {
        parentMachine->entryNodeId = nodeId;
        ui.dirty = true;
    }

    if (node->type == engine::AnimationTreeNode::Type::StateMachine && ImGui::MenuItem("Enter Sub-State Machine"))
    {
        ui.currentMachineNodeId = node->id;
        ui.selectedNodeId = -1;
        ui.selectedTransitionIndex = -1;
        ui.selectedTransitionMachineNodeId = -1;
    }

    if (ImGui::MenuItem("Rename"))
    {
        ui.renamePopupOpen = true;
        ui.renameNodeId = nodeId;
        std::strncpy(ui.renameBuffer, node->name.c_str(), sizeof(ui.renameBuffer) - 1);
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Delete"))
    {
        ed::DeleteNode(ed::NodeId(graphNodeId(nodeId)));
    }

    ImGui::EndPopup();

    if (ui.renamePopupOpen && ui.renameNodeId == nodeId)
    {
        ImGui::OpenPopup(("RenameNode##" + std::to_string(nodeId)).c_str());
        ui.renamePopupOpen = false;
    }

    if (ImGui::BeginPopupModal(("RenameNode##" + std::to_string(nodeId)).c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Name", ui.renameBuffer, sizeof(ui.renameBuffer));
        if (ImGui::Button("OK") && ui.renameBuffer[0] != '\0')
        {
            if (engine::AnimationTreeNode *renameNode = ui.tree.findNode(nodeId))
                renameNode->name = ui.renameBuffer;
            ui.dirty = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void AnimationTreePanel::drawTransitionInspector(AnimTreeUIState &ui)
{
    ui.tree.ensureGraph();

    if (engine::AnimationTreeNode *machineNode = ui.tree.findNode(ui.selectedTransitionMachineNodeId);
        machineNode && machineNode->isStateMachine() &&
        ui.selectedTransitionIndex >= 0 &&
        ui.selectedTransitionIndex < static_cast<int>(machineNode->transitions.size()))
    {
        auto &transition = machineNode->transitions[static_cast<size_t>(ui.selectedTransitionIndex)];

        auto childNameForId = [&ui, machineNode](int nodeId) -> std::string
        {
            if (nodeId == engine::AnimationTree::ANY_NODE_ID)
                return "Any";
            if (std::find(machineNode->childNodeIds.begin(), machineNode->childNodeIds.end(), nodeId) ==
                machineNode->childNodeIds.end())
                return "?";
            if (const auto *node = ui.tree.findNode(nodeId))
                return node->name;
            return "?";
        };

        const std::string fromName = childNameForId(transition.fromNodeId);
        const std::string toName = childNameForId(transition.toNodeId);

        ImGui::Text("Transition: %s  ->  %s", fromName.c_str(), toName.c_str());
        ImGui::Separator();

        if (ImGui::BeginCombo("From", fromName.c_str()))
        {
            if (ImGui::Selectable("Any", transition.fromNodeId == engine::AnimationTree::ANY_NODE_ID))
            {
                transition.fromNodeId = engine::AnimationTree::ANY_NODE_ID;
                ui.dirty = true;
            }

            for (int childNodeId : machineNode->childNodeIds)
            {
                const auto *childNode = ui.tree.findNode(childNodeId);
                if (!childNode)
                    continue;

                ImGui::BeginDisabled(childNodeId == transition.toNodeId);
                if (ImGui::Selectable(childNode->name.c_str(), transition.fromNodeId == childNodeId))
                {
                    transition.fromNodeId = childNodeId;
                    ui.dirty = true;
                }
                ImGui::EndDisabled();
            }

            ImGui::EndCombo();
        }

        if (ImGui::BeginCombo("To", toName.c_str()))
        {
            for (int childNodeId : machineNode->childNodeIds)
            {
                const auto *childNode = ui.tree.findNode(childNodeId);
                if (!childNode)
                    continue;

                ImGui::BeginDisabled(childNodeId == transition.fromNodeId);
                if (ImGui::Selectable(childNode->name.c_str(), transition.toNodeId == childNodeId))
                {
                    transition.toNodeId = childNodeId;
                    ui.dirty = true;
                }
                ImGui::EndDisabled();
            }

            ImGui::EndCombo();
        }

        if (ImGui::DragFloat("Blend Duration (s)", &transition.blendDuration, 0.01f, 0.0f, 5.0f, "%.2f"))
            ui.dirty = true;
        if (ImGui::Checkbox("Has Exit Time", &transition.hasExitTime))
            ui.dirty = true;
        if (transition.hasExitTime)
        {
            if (ImGui::SliderFloat("Exit Time", &transition.exitTime, 0.0f, 1.0f))
                ui.dirty = true;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Conditions:");

        if (ui.tree.parameters.empty())
            ImGui::TextDisabled("Add parameters like speed or use On Over for one-shot states.");

        int removeCondIdx = -1;
        for (int ci = 0; ci < static_cast<int>(transition.conditions.size()); ++ci)
        {
            auto &cond = transition.conditions[static_cast<size_t>(ci)];
            ImGui::PushID(ci);

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("If");
            ImGui::SameLine();

            const bool isStateFinishedCondition = cond.type == engine::AnimationTransitionCondition::Type::StateFinished;
            const char *curParamName = isStateFinishedCondition
                                           ? "On Over"
                                           : (cond.parameterName.empty() ? "<select>" : cond.parameterName.c_str());
            const engine::AnimationTreeParameter *selectedParam = findAnimationTreeParameter(ui.tree, cond.parameterName);
            engine::AnimationTreeParameter::Type parameterType = engine::AnimationTreeParameter::Type::Float;
            bool hasSelectedParameter = (selectedParam != nullptr);

            if (!isStateFinishedCondition && selectedParam)
            {
                parameterType = selectedParam->type;
            }
            else if (!isStateFinishedCondition)
            {
                switch (cond.type)
                {
                case engine::AnimationTransitionCondition::Type::BoolTrue:
                case engine::AnimationTransitionCondition::Type::BoolFalse:
                    parameterType = engine::AnimationTreeParameter::Type::Bool;
                    break;
                case engine::AnimationTransitionCondition::Type::IntEqual:
                case engine::AnimationTransitionCondition::Type::IntGreater:
                case engine::AnimationTransitionCondition::Type::IntLess:
                    parameterType = engine::AnimationTreeParameter::Type::Int;
                    break;
                case engine::AnimationTransitionCondition::Type::Trigger:
                    parameterType = engine::AnimationTreeParameter::Type::Trigger;
                    break;
                case engine::AnimationTransitionCondition::Type::FloatGreater:
                case engine::AnimationTransitionCondition::Type::FloatLess:
                case engine::AnimationTransitionCondition::Type::FloatEqual:
                case engine::AnimationTransitionCondition::Type::StateFinished:
                default:
                    parameterType = engine::AnimationTreeParameter::Type::Float;
                    break;
                }
            }

            const ImGuiStyle &style = ImGui::GetStyle();
            const bool hasNumericThreshold = !isStateFinishedCondition &&
                                             (parameterType == engine::AnimationTreeParameter::Type::Float ||
                                              parameterType == engine::AnimationTreeParameter::Type::Int);
            const float controlsWidth = ImGui::GetContentRegionAvail().x;
            const float removeWidth = ImGui::CalcTextSize("Remove").x + style.FramePadding.x * 2.0f;
            const float opWidth = isStateFinishedCondition ? 90.0f
                                  : (parameterType == engine::AnimationTreeParameter::Type::Bool) ? 95.0f
                                  : (parameterType == engine::AnimationTreeParameter::Type::Trigger) ? 70.0f
                                                                                                       : 60.0f;
            const float valueWidth = hasNumericThreshold ? glm::clamp(controlsWidth * 0.16f, 80.0f, 130.0f) : 0.0f;
            const float spacingWidth = style.ItemSpacing.x * (hasNumericThreshold ? 3.0f : 2.0f);
            const float paramWidth = glm::max(140.0f, controlsWidth - removeWidth - opWidth - valueWidth - spacingWidth);

            ImGui::SetNextItemWidth(paramWidth);

            if (ImGui::BeginCombo("##parameter", curParamName))
            {
                if (ImGui::Selectable("On Over", isStateFinishedCondition))
                {
                    cond.type = engine::AnimationTransitionCondition::Type::StateFinished;
                    cond.parameterName.clear();
                    ui.dirty = true;
                }

                for (const auto &param : ui.tree.parameters)
                {
                    bool sel = (param.name == cond.parameterName);
                    if (ImGui::Selectable(param.name.c_str(), sel))
                    {
                        cond.parameterName = param.name;
                        switch (param.type)
                        {
                        case engine::AnimationTreeParameter::Type::Float:
                            cond.type = engine::AnimationTransitionCondition::Type::FloatGreater;
                            break;
                        case engine::AnimationTreeParameter::Type::Bool:
                            cond.type = engine::AnimationTransitionCondition::Type::BoolTrue;
                            break;
                        case engine::AnimationTreeParameter::Type::Int:
                            cond.type = engine::AnimationTransitionCondition::Type::IntEqual;
                            break;
                        case engine::AnimationTreeParameter::Type::Trigger:
                            cond.type = engine::AnimationTransitionCondition::Type::Trigger;
                            break;
                        }
                        ui.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::SetNextItemWidth(opWidth);

            switch (parameterType)
            {
            case engine::AnimationTreeParameter::Type::Float:
            {
                if (isStateFinishedCondition)
                {
                    ImGui::TextDisabled("is over");
                    break;
                }

                int opIdx = 0;
                if (cond.type == engine::AnimationTransitionCondition::Type::FloatLess)
                    opIdx = 1;
                else if (cond.type == engine::AnimationTransitionCondition::Type::FloatEqual)
                    opIdx = 2;

                if (ImGui::Combo("##op", &opIdx, ">\0<\0==\0"))
                {
                    switch (opIdx)
                    {
                    case 1:
                        cond.type = engine::AnimationTransitionCondition::Type::FloatLess;
                        break;
                    case 2:
                        cond.type = engine::AnimationTransitionCondition::Type::FloatEqual;
                        break;
                    case 0:
                    default:
                        cond.type = engine::AnimationTransitionCondition::Type::FloatGreater;
                        break;
                    }
                    ui.dirty = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(valueWidth);
                if (ImGui::DragFloat("##fthresh", &cond.floatThreshold, 0.01f, 0.0f, 0.0f, "%.2f"))
                    ui.dirty = true;
                break;
            }
            case engine::AnimationTreeParameter::Type::Bool:
            {
                int opIdx = (cond.type == engine::AnimationTransitionCondition::Type::BoolTrue) ? 0 : 1;
                if (ImGui::Combo("##boolop", &opIdx, "is true\0is false\0"))
                {
                    cond.type = (opIdx == 0) ? engine::AnimationTransitionCondition::Type::BoolTrue
                                             : engine::AnimationTransitionCondition::Type::BoolFalse;
                    ui.dirty = true;
                }
                break;
            }
            case engine::AnimationTreeParameter::Type::Int:
            {
                int opIdx = 0;
                if (cond.type == engine::AnimationTransitionCondition::Type::IntGreater)
                    opIdx = 1;
                else if (cond.type == engine::AnimationTransitionCondition::Type::IntLess)
                    opIdx = 2;

                if (ImGui::Combo("##intop", &opIdx, "==\0>\0<\0"))
                {
                    switch (opIdx)
                    {
                    case 1:
                        cond.type = engine::AnimationTransitionCondition::Type::IntGreater;
                        break;
                    case 2:
                        cond.type = engine::AnimationTransitionCondition::Type::IntLess;
                        break;
                    case 0:
                    default:
                        cond.type = engine::AnimationTransitionCondition::Type::IntEqual;
                        break;
                    }
                    ui.dirty = true;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(valueWidth);
                if (ImGui::InputInt("##ival", &cond.intValue))
                    ui.dirty = true;
                break;
            }
            case engine::AnimationTreeParameter::Type::Trigger:
                ImGui::TextDisabled("fires");
                break;
            }

            ImGui::SameLine();
            if (ImGui::Button("Remove##cond"))
                removeCondIdx = ci;

            if (!hasSelectedParameter && !isStateFinishedCondition)
                ImGui::TextDisabled("Select a parameter to finish this condition.");

            if (ci + 1 < static_cast<int>(transition.conditions.size()))
                ImGui::Separator();

            ImGui::PopID();
        }

        if (removeCondIdx >= 0)
        {
            transition.conditions.erase(transition.conditions.begin() + removeCondIdx);
            ui.dirty = true;
        }

        if (ImGui::Button("+ Add Condition"))
        {
            engine::AnimationTransitionCondition cond{};
            if (!ui.tree.parameters.empty())
            {
                const auto &firstParam = ui.tree.parameters.front();
                cond.parameterName = firstParam.name;
                switch (firstParam.type)
                {
                case engine::AnimationTreeParameter::Type::Float:
                    cond.type = engine::AnimationTransitionCondition::Type::FloatGreater;
                    break;
                case engine::AnimationTreeParameter::Type::Bool:
                    cond.type = engine::AnimationTransitionCondition::Type::BoolTrue;
                    break;
                case engine::AnimationTreeParameter::Type::Int:
                    cond.type = engine::AnimationTransitionCondition::Type::IntEqual;
                    break;
                case engine::AnimationTreeParameter::Type::Trigger:
                    cond.type = engine::AnimationTransitionCondition::Type::Trigger;
                    break;
                }
            }
            else
            {
                cond.type = engine::AnimationTransitionCondition::Type::StateFinished;
            }
            transition.conditions.push_back(cond);
            ui.dirty = true;
        }

        return;
    }

    if (engine::AnimationTreeNode *selectedNode = ui.tree.findNode(ui.selectedNodeId))
    {
        ImGui::Text("Node: %s", selectedNode->name.c_str());
        ImGui::TextDisabled("%s", animationTreeNodeTypeLabel(selectedNode->type));
        ImGui::Separator();

        if (selectedNode->type == engine::AnimationTreeNode::Type::StateMachine)
        {
            ImGui::Text("Children: %zu", selectedNode->childNodeIds.size());
            ImGui::Text("Transitions: %zu", selectedNode->transitions.size());
            if (ImGui::Button("Enter Sub-State Machine"))
            {
                ui.currentMachineNodeId = selectedNode->id;
                ui.selectedNodeId = -1;
                ui.selectedTransitionIndex = -1;
                ui.selectedTransitionMachineNodeId = -1;
            }

            engine::AnimationTreeNode *entryChild = ui.tree.findNode(selectedNode->entryNodeId);
            const char *currentEntryLabel = entryChild ? entryChild->name.c_str() : "(none)";
            if (ImGui::BeginCombo("Entry Child", currentEntryLabel))
            {
                for (int childNodeId : selectedNode->childNodeIds)
                {
                    const auto *childNode = ui.tree.findNode(childNodeId);
                    if (!childNode)
                        continue;
                    if (ImGui::Selectable(childNode->name.c_str(), selectedNode->entryNodeId == childNodeId))
                    {
                        selectedNode->entryNodeId = childNodeId;
                        ui.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            return;
        }

        if (selectedNode->type == engine::AnimationTreeNode::Type::ClipState)
        {
            const std::string clipText = selectedNode->animationAssetPath.empty()
                                             ? "Drop .anim here"
                                             : std::filesystem::path(selectedNode->animationAssetPath).stem().string();
            ImGui::Button(clipText.c_str(), ImVec2(-1.0f, 0.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    const std::string dropped(static_cast<const char *>(payload->Data),
                                              static_cast<size_t>(payload->DataSize) - 1u);
                    if (dropped.find(".anim.elixasset") != std::string::npos)
                    {
                        selectedNode->animationAssetPath = dropped;
                        ui.dirty = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::SmallButton("Clear Clip"))
            {
                selectedNode->animationAssetPath.clear();
                ui.dirty = true;
            }
            if (ImGui::InputInt("Clip Index", &selectedNode->clipIndex))
                ui.dirty = true;
            if (ImGui::Checkbox("Loop", &selectedNode->loop))
                ui.dirty = true;
            if (ImGui::DragFloat("Speed", &selectedNode->speed, 0.01f, 0.01f, 5.0f, "%.2f"))
                ui.dirty = true;
            return;
        }

        if (selectedNode->type == engine::AnimationTreeNode::Type::BlendSpace1D)
        {
            const char *currentParamLabel =
                selectedNode->blendParameterName.empty() ? "<select float parameter>" : selectedNode->blendParameterName.c_str();
            if (ImGui::BeginCombo("Blend Parameter", currentParamLabel))
            {
                for (const auto &parameter : ui.tree.parameters)
                {
                    if (parameter.type != engine::AnimationTreeParameter::Type::Float)
                        continue;
                    if (ImGui::Selectable(parameter.name.c_str(), selectedNode->blendParameterName == parameter.name))
                    {
                        selectedNode->blendParameterName = parameter.name;
                        ui.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Checkbox("Loop", &selectedNode->loop))
                ui.dirty = true;
            if (ImGui::DragFloat("Speed", &selectedNode->speed, 0.01f, 0.01f, 5.0f, "%.2f"))
                ui.dirty = true;

            ImGui::Separator();
            ImGui::TextUnformatted("Samples");

            int removeSampleIndex = -1;
            for (int sampleIndex = 0; sampleIndex < static_cast<int>(selectedNode->blendSamples.size()); ++sampleIndex)
            {
                auto &sample = selectedNode->blendSamples[static_cast<size_t>(sampleIndex)];
                ImGui::PushID(sampleIndex);

                if (ImGui::DragFloat("Position", &sample.position, 0.01f))
                    ui.dirty = true;

                const std::string clipLabel = sample.animationAssetPath.empty()
                                                  ? "Drop .anim here"
                                                  : std::filesystem::path(sample.animationAssetPath).stem().string();
                ImGui::Button(clipLabel.c_str(), ImVec2(-90.0f, 0.0f));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                    {
                        const std::string dropped(static_cast<const char *>(payload->Data),
                                                  static_cast<size_t>(payload->DataSize) - 1u);
                        if (dropped.find(".anim.elixasset") != std::string::npos)
                        {
                            sample.animationAssetPath = dropped;
                            ui.dirty = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::InputInt("Clip Index", &sample.clipIndex))
                    ui.dirty = true;
                if (ImGui::SmallButton("Remove Sample"))
                    removeSampleIndex = sampleIndex;

                if (sampleIndex + 1 < static_cast<int>(selectedNode->blendSamples.size()))
                    ImGui::Separator();

                ImGui::PopID();
            }

            if (removeSampleIndex >= 0)
            {
                selectedNode->blendSamples.erase(selectedNode->blendSamples.begin() + removeSampleIndex);
                ui.dirty = true;
            }

            if (ImGui::Button("+ Add Sample"))
            {
                selectedNode->blendSamples.push_back(engine::AnimationBlendSpace1DSample{});
                ui.dirty = true;
            }

            return;
        }
    }

    ImGui::TextDisabled("Select a node or transition to inspect it.");
}

void AnimationTreePanel::update(float deltaTime)
{
    bool submittedPreview = false;

    for (auto &[key, ui] : m_uiStates)
    {
        auto &preview = ui.preview;
        if (!preview.active || !preview.skeletalMesh)
            continue;

        if (!ensurePreviewMeshSourceLoaded(preview))
            continue;

        const size_t treeHash = computeAnimationTreeHash(ui.tree);
        const bool skeletonShapeChanged =
            preview.runtimeReady &&
            preview.previewSkeleton.getBonesCount() != preview.skeletalMesh->getSkeleton().getBonesCount();

        if (skeletonShapeChanged)
        {
            preview.runtimeReady = false;
            preview.syncedTreeHash = 0u;
            preview.previewMeshes.clear();
            preview.modelMatrix = glm::mat4(1.0f);
        }

        if (!preview.runtimeReady)
        {
            preview.previewSkeleton = preview.skeletalMesh->getSkeleton();
            preview.previewSkeleton.calculateBindPoseTransforms();
            preview.previewAnimator.bindSkeleton(&preview.previewSkeleton);
            preview.syncedTreeHash = 0u;
            preview.runtimeReady = true;
        }

        if (preview.syncedTreeHash != treeHash)
        {
            preview.previewSkeleton = preview.skeletalMesh->getSkeleton();
            preview.previewSkeleton.calculateBindPoseTransforms();
            preview.previewAnimator.bindSkeleton(&preview.previewSkeleton);
            preview.previewAnimator.setTree(ui.tree);
            preview.syncedTreeHash = treeHash;
            preview.previewAnimator.update(0.0f);
        }

        // Lazily create GPU meshes the first time update() runs (safe Vulkan context)
        if (preview.previewMeshes.empty() && preview.skeletalMesh)
        {
            const auto &cpuMeshes = preview.skeletalMesh->getMeshes();

            // Build candidates from all meshes
            std::vector<PreviewCandidateInfo> candidates;
            candidates.reserve(cpuMeshes.size());
            for (size_t slot = 0; slot < cpuMeshes.size(); ++slot)
            {
                PreviewCandidateInfo candidate{};
                if (buildPreviewCandidate(cpuMeshes[slot], slot, candidate))
                    candidates.push_back(std::move(candidate));
            }

            if (!candidates.empty())
            {
                // Find the best candidate by score (largest bounding diagonal = most body)
                float bestScore = -1.0f;
                for (const auto &c : candidates)
                    if (c.score > bestScore)
                        bestScore = c.score;

                // Include all candidates that are at least 10% the size of the primary.
                // This collects body + clothing + accessories while excluding tiny decals/props.
                const float scoreThreshold = bestScore * 0.1f;

                glm::vec3 combinedMin(std::numeric_limits<float>::max());
                glm::vec3 combinedMax(std::numeric_limits<float>::lowest());

                for (const auto &candidate : candidates)
                {
                    if (candidate.score < scoreThreshold)
                        continue;

                    auto gpu = engine::GPUMesh::createFromMesh(candidate.previewMesh);
                    if (!gpu)
                        continue;

                    auto material = preview.skeletalMesh->getMaterialOverride(candidate.slot);
                    if (!material)
                        material = createPreviewMaterial(candidate.previewMesh.material,
                                                         preview.skeletalMesh->getAssetPath());
                    gpu->material = material ? material : engine::Material::getDefaultMaterial();

                    PreviewMeshEntry entry{};
                    entry.gpuMesh = std::move(gpu);
                    entry.material = entry.gpuMesh->material
                                         ? entry.gpuMesh->material
                                         : engine::Material::getDefaultMaterial();
                    entry.sourceMesh = cpuMeshes[candidate.slot];
                    entry.localTransform = candidate.previewMesh.localTransform;
                    entry.attachedBoneId = candidate.previewMesh.attachedBoneId;
                    entry.skinned = candidate.skinned;
                    if (entry.skinned)
                        entry.dynamicVertices.resize(
                            candidate.previewMesh.vertexData.size() / sizeof(engine::vertex::Vertex3D));

                    combinedMin = glm::min(combinedMin, candidate.boundsMin);
                    combinedMax = glm::max(combinedMax, candidate.boundsMax);

                    preview.previewMeshes.push_back(std::move(entry));
                }

                // Fallback: if nothing passed the threshold, add every candidate
                if (preview.previewMeshes.empty())
                {
                    for (const auto &candidate : candidates)
                    {
                        auto gpu = engine::GPUMesh::createFromMesh(candidate.previewMesh);
                        if (!gpu)
                            continue;

                        auto material = createPreviewMaterial(candidate.previewMesh.material,
                                                              preview.skeletalMesh->getAssetPath());
                        gpu->material = material ? material : engine::Material::getDefaultMaterial();

                        PreviewMeshEntry entry{};
                        entry.gpuMesh = std::move(gpu);
                        entry.material = entry.gpuMesh->material
                                             ? entry.gpuMesh->material
                                             : engine::Material::getDefaultMaterial();
                        entry.sourceMesh = cpuMeshes[candidate.slot];
                        entry.localTransform = candidate.previewMesh.localTransform;
                        entry.attachedBoneId = candidate.previewMesh.attachedBoneId;
                        entry.skinned = candidate.skinned;
                        if (entry.skinned)
                            entry.dynamicVertices.resize(
                                candidate.previewMesh.vertexData.size() / sizeof(engine::vertex::Vertex3D));

                        combinedMin = glm::min(combinedMin, candidate.boundsMin);
                        combinedMax = glm::max(combinedMax, candidate.boundsMax);

                        preview.previewMeshes.push_back(std::move(entry));
                    }
                }

                if (combinedMin.x <= combinedMax.x)
                    preview.modelMatrix = buildPreviewNormalizationTransform(combinedMin, combinedMax);
            }
        }

        if (preview.isFirstActivation)
        {
            resetPreviewCamera(preview);
            preview.isFirstActivation = false;
        }

        const float previewDeltaTime = preview.playing ? deltaTime * glm::max(preview.playbackSpeed, 0.0f) : 0.0f;
        preview.previewAnimator.update(previewDeltaTime);

        if (!m_previewPass)
            continue;

        // Build draw data from cached GPU meshes
        AnimPreviewDrawData data{};
        data.modelMatrix = preview.modelMatrix;
        data.viewMatrix = buildOrbitView(preview);
        data.projMatrix = buildOrbitProj();

        const auto &finalBones = preview.previewSkeleton.getFinalMatrices();

        for (auto &previewMesh : preview.previewMeshes)
        {
            auto *gpu = previewMesh.gpuMesh.get();
            if (!gpu)
                continue;

            if (previewMesh.skinned)
            {
                updateSkinnedPreviewVertices(previewMesh.sourceMesh, finalBones, previewMesh.dynamicVertices);
                if (!previewMesh.dynamicVertices.empty())
                    gpu->vertexBuffer->upload(previewMesh.dynamicVertices.data(),
                                              static_cast<VkDeviceSize>(previewMesh.dynamicVertices.size() * sizeof(engine::vertex::Vertex3D)));
            }

            glm::mat4 meshLocalTransform = previewMesh.localTransform;
            if (previewMesh.attachedBoneId >= 0)
            {
                if (auto *attachmentBone = preview.previewSkeleton.getBone(previewMesh.attachedBoneId))
                    meshLocalTransform = attachmentBone->finalTransformation * meshLocalTransform;
            }

            // Match the real skeletal renderer: every mesh keeps its own local
            // transform even when the vertices are skinned.
            const glm::mat4 meshModel = preview.modelMatrix * meshLocalTransform;

            data.meshes.push_back(gpu);
            data.materials.push_back(previewMesh.material.get());
            data.meshModelMatrices.push_back(meshModel);
        }

        data.hasData = !data.meshes.empty();
        m_previewPass->setPreviewData(data);
        submittedPreview = true;
        break; // one active preview at a time
    }

    if (m_previewPass && !submittedPreview)
        m_previewPass->setPreviewData(AnimPreviewDrawData{});
}

void AnimationTreePanel::drawPreviewPane(AnimTreeUIState &ui)
{
    ImGui::Separator();
    ImGui::TextDisabled("Preview");

    auto &preview = ui.preview;

    if (!preview.active || !preview.skeletalMesh)
    {
        ImGui::TextDisabled("Open this tree via an entity's AnimatorComponent to enable preview.");
        return;
    }

    const float avail = ImGui::GetContentRegionAvail().x;
    const float remainingY = ImGui::GetContentRegionAvail().y;
    const float previewH = glm::clamp(glm::min(avail, remainingY * 0.6f), 180.0f, glm::max(180.0f, remainingY));
    const ImVec2 previewSize(avail, previewH);

    refreshLivePreviewDescriptor();

    const VkDescriptorSet previewDescriptor =
        (m_livePreviewDescriptorSet != VK_NULL_HANDLE) ? m_livePreviewDescriptorSet : m_sharedPreviewDescriptorSet;
    if (previewDescriptor != VK_NULL_HANDLE)
    {
        ImGui::Image((ImTextureID)(uintptr_t)previewDescriptor, previewSize);

        if (ImGui::IsItemHovered())
        {
            // Orbit with right mouse drag
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            {
                const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
                preview.yaw -= delta.x * 0.5f;
                preview.pitch -= delta.y * 0.5f;
                preview.pitch = glm::clamp(preview.pitch, -89.0f, 89.0f);
            }

            // Pan the camera target with middle mouse drag
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle, 0.0f);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);

                const float yawRad = glm::radians(preview.yaw);
                const float pitchRad = glm::radians(preview.pitch);
                const glm::vec3 offset{
                    preview.distance * std::cos(pitchRad) * std::sin(yawRad),
                    preview.distance * std::sin(pitchRad),
                    preview.distance * std::cos(pitchRad) * std::cos(yawRad)};
                const glm::vec3 forward = glm::normalize(-offset);
                const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
                const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
                const glm::vec3 up = glm::normalize(glm::cross(right, forward));
                const float worldUnitsPerPixel =
                    (2.0f * std::tan(glm::radians(45.0f) * 0.5f) * glm::max(preview.distance, 0.001f)) /
                    glm::max(previewSize.y, 1.0f);

                preview.target += (-delta.x * right + delta.y * up) * worldUnitsPerPixel;
            }

            // Zoom with scroll
            const float scroll = ImGui::GetIO().MouseWheel;
            if (scroll != 0.0f)
                preview.distance = glm::clamp(preview.distance - scroll * 0.45f, kPreviewMinDistance, kPreviewMaxDistance);
        }
    }
    else
    {
        ImGui::TextDisabled("(preview not ready)");
    }

    if (ImGui::Button(preview.playing ? "Pause" : "Play"))
        preview.playing = !preview.playing;

    ImGui::SameLine();
    if (ImGui::Button("Restart"))
    {
        preview.runtimeReady = false;
        preview.syncedTreeHash = 0u;
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Reset Camera"))
        resetPreviewCamera(preview);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::DragFloat("Speed##AnimTreePreviewSpeed", &preview.playbackSpeed, 0.01f, 0.05f, 3.0f, "%.2fx");

    // State info
    const std::string stateName = preview.previewAnimator.getCurrentStateName();
    const std::string statePath = preview.previewAnimator.getCurrentStatePath();
    const std::string machinePath = preview.previewAnimator.getActiveMachinePath();
    ImGui::Text("State: %s", stateName.empty() ? "(none)" : stateName.c_str());
    ImGui::TextDisabled("State Path: %s", statePath.empty() ? "(none)" : statePath.c_str());
    ImGui::TextDisabled("Machine Path: %s", machinePath.empty() ? "(none)" : machinePath.c_str());
    if (preview.previewAnimator.isInTransition())
        ImGui::TextDisabled("Transitioning %.0f%%", preview.previewAnimator.getCurrentStateNormalizedTime() * 100.0f);
    ImGui::TextDisabled("RMB orbit, MMB pan, wheel zoom");

    if (preview.skeletalMesh->getMeshes().empty())
        ImGui::TextDisabled("Waiting for skeletal mesh data from the entity...");

    if (ui.tree.parameters.empty())
        return;

    ImGui::Spacing();
    ImGui::TextDisabled("Runtime Parameters");
    ImGui::Separator();

    for (const auto &param : ui.tree.parameters)
    {
        ImGui::PushID(param.name.c_str());

        switch (param.type)
        {
        case engine::AnimationTreeParameter::Type::Float:
        {
            float value = preview.previewAnimator.getFloat(param.name);
            if (ImGui::DragFloat(param.name.c_str(), &value, 0.01f))
            {
                preview.previewAnimator.setFloat(param.name, value);
                preview.previewAnimator.update(0.0f);
            }
            break;
        }
        case engine::AnimationTreeParameter::Type::Bool:
        {
            bool value = preview.previewAnimator.getBool(param.name);
            if (ImGui::Checkbox(param.name.c_str(), &value))
            {
                preview.previewAnimator.setBool(param.name, value);
                preview.previewAnimator.update(0.0f);
            }
            break;
        }
        case engine::AnimationTreeParameter::Type::Int:
        {
            int value = preview.previewAnimator.getInt(param.name);
            if (ImGui::InputInt(param.name.c_str(), &value))
            {
                preview.previewAnimator.setInt(param.name, value);
                preview.previewAnimator.update(0.0f);
            }
            break;
        }
        case engine::AnimationTreeParameter::Type::Trigger:
        {
            if (ImGui::Button(("Fire: " + param.name).c_str()))
            {
                preview.previewAnimator.setTrigger(param.name);
                preview.previewAnimator.update(0.0f);
            }
            break;
        }
        }

        ImGui::PopID();
    }
}

void AnimationTreePanel::resetPreviewCamera(AnimPreviewContext &ctx)
{
    ctx.target = glm::vec3(0.0f, 0.35f, 0.0f);
    ctx.yaw = 18.0f;
    ctx.pitch = 14.0f;
    ctx.distance = kPreviewMaxDistance;
}

glm::mat4 AnimationTreePanel::buildOrbitView(const AnimPreviewContext &ctx) const
{
    const float yawRad = glm::radians(ctx.yaw);
    const float pitchRad = glm::radians(ctx.pitch);

    const glm::vec3 offset{
        ctx.distance * std::cos(pitchRad) * std::sin(yawRad),
        ctx.distance * std::sin(pitchRad),
        ctx.distance * std::cos(pitchRad) * std::cos(yawRad)};

    const glm::vec3 eye = ctx.target + offset;
    return glm::lookAt(eye, ctx.target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 AnimationTreePanel::buildOrbitProj() const
{
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.05f, 100.0f);
    proj[1][1] *= -1.0f; // Vulkan Y-flip
    return proj;
}

ELIX_NESTED_NAMESPACE_END
