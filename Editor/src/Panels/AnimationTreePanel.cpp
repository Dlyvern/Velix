#include "Editor/Panels/AnimationTreePanel.hpp"
#include "Editor/Project.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Material.hpp"
#include "Engine/Vertex.hpp"

#include "imgui.h"
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
        const float scale = 1.8f / maxAxis;
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

        if (hasPreviewSource)
        {
            ui.preview.animator = animator;
            ui.preview.skeletalMesh = skeletalMesh;
            ui.preview.active = true;
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

void AnimationTreePanel::draw()
{
    for (auto &editor : m_openEditors)
        drawSingleEditor(editor);

    m_openEditors.erase(
        std::remove_if(m_openEditors.begin(), m_openEditors.end(),
                       [](const OpenTreeEditor &e)
                       { return !e.open; }),
        m_openEditors.end());
}

void AnimationTreePanel::drawSingleEditor(OpenTreeEditor &editor)
{
    const std::string key = editor.path.lexically_normal().string();
    const std::string stem = editor.path.stem().string();

    auto it = m_uiStates.find(key);
    if (it == m_uiStates.end())
        return;
    AnimTreeUIState &ui = it->second;

    const std::string windowTitle = (ui.dirty ? "* " : "") + stem + " [Animation Tree]##" + key;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::SetNextWindowDockID(m_centerDockId, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(windowTitle.c_str(), &editor.open, flags))
    {
        ImGui::End();
        return;
    }

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

    if (!ui.initialized)
    {
        ed::Config config;
        config.SettingsFile = nullptr;
        ui.nodeEditorContext = ed::CreateEditor(&config);
        ui.initialized = true;

        if (ui.nodeEditorContext)
        {
            ed::SetCurrentEditor(ui.nodeEditorContext);
            ed::SetNodePosition(ed::NodeId(1), ImVec2(20.0f, 80.0f)); // Entry
            for (int i = 0; i < static_cast<int>(ui.tree.states.size()); ++i)
            {
                const glm::vec2 pos = (i < static_cast<int>(ui.tree.stateNodePositions.size()))
                                          ? ui.tree.stateNodePositions[static_cast<size_t>(i)]
                                          : glm::vec2(280.0f + i * 220.0f, 80.0f);
                ed::SetNodePosition(ed::NodeId(stateNodeId(i)), ImVec2(pos.x, pos.y));
            }
            ed::SetCurrentEditor(nullptr);
        }
    }

    ed::SetCurrentEditor(ui.nodeEditorContext);
    ed::Begin(("AnimTree##" + key).c_str(), ImVec2(0.0f, 0.0f));

    // Entry node
    ed::BeginNode(ed::NodeId(1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.3f, 1.0f));
    ImGui::Text("Entry");
    ImGui::PopStyleColor();
    ed::BeginPin(ed::PinId(2), ed::PinKind::Output);
    ImGui::Text("Start >");
    ed::EndPin();
    ed::EndNode();

    // State nodes
    for (int i = 0; i < static_cast<int>(ui.tree.states.size()); ++i)
    {
        auto &state = ui.tree.states[static_cast<size_t>(i)];
        const bool isEntry = (i == ui.tree.entryStateIndex);

        ed::BeginNode(ed::NodeId(stateNodeId(i)));

        if (isEntry)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));

        ImGui::Text("%s", state.name.c_str());

        if (isEntry)
            ImGui::PopStyleColor();

        // Clip drop zone — selectable so it registers hover for drag-drop
        {
            const std::string clipSelectId = "##clipdrop" + std::to_string(i);
            const std::string clipText = state.animationAssetPath.empty()
                                             ? "Drop .anim here"
                                             : std::filesystem::path(state.animationAssetPath).stem().string();

            if (state.animationAssetPath.empty())
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));

            ImGui::Selectable((clipText + clipSelectId).c_str(), false,
                              ImGuiSelectableFlags_None, ImVec2(140.0f, 0.0f));

            if (state.animationAssetPath.empty())
                ImGui::PopStyleColor();

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    const std::string dropped(static_cast<const char *>(payload->Data),
                                              static_cast<size_t>(payload->DataSize) - 1u);
                    if (dropped.find(".anim.elixasset") != std::string::npos)
                    {
                        state.animationAssetPath = dropped;
                        ui.dirty = true;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        // Pins: input on the left, output (drag from here) on the right
        ed::BeginPin(ed::PinId(stateInPin(i)), ed::PinKind::Input);
        ImGui::Text("->");
        ed::EndPin();
        ImGui::SameLine();
        ed::BeginPin(ed::PinId(stateOutPin(i)), ed::PinKind::Output);
        ImGui::Text("o-");
        ed::EndPin();

        ed::EndNode();

        // Context menu on right-click
        if (ed::GetDoubleClickedNode() == ed::NodeId(stateNodeId(i)) ||
            ImGui::IsItemHovered())
        {
            // handled via ed::ShowBackgroundContextMenu below
        }
    }

    // Entry -> entry state link must be drawn after state nodes so both pins
    // are live in the current frame.
    if (!ui.tree.states.empty() && ui.tree.entryStateIndex >= 0 &&
        ui.tree.entryStateIndex < static_cast<int>(ui.tree.states.size()))
    {
        ed::Link(ed::LinkId(999),
                 ed::PinId(2),
                 ed::PinId(stateInPin(ui.tree.entryStateIndex)),
                 ImVec4(0.2f, 0.9f, 0.3f, 1.0f), 2.0f);
    }

    // Transition links
    for (int i = 0; i < static_cast<int>(ui.tree.transitions.size()); ++i)
    {
        const auto &t = ui.tree.transitions[static_cast<size_t>(i)];
        const int fromOut = (t.fromStateIndex == -1) ? 2 : stateOutPin(t.fromStateIndex);
        const int toIn = stateInPin(t.toStateIndex);
        const bool selected = (ui.selectedTransitionIndex == i);
        const ImVec4 color = selected ? ImVec4(1.0f, 0.7f, 0.1f, 1.0f) : ImVec4(0.7f, 0.7f, 0.9f, 1.0f);
        ed::Link(ed::LinkId(transitionLinkId(i)), ed::PinId(fromOut), ed::PinId(toIn), color, selected ? 2.5f : 1.5f);
    }

    // Helpers defined outside so they can be used for both validation and creation
    auto pinToStateOut = [&](ed::PinId pin) -> int
    {
        const int raw = static_cast<int>(pin.Get());
        if (raw == 2)
            return -2; // entry output pin
        if (raw >= 300 && raw < 400)
            return raw - 300; // state output pin
        return -1;
    };
    auto pinToStateIn = [&](ed::PinId pin) -> int
    {
        const int raw = static_cast<int>(pin.Get());
        if (raw >= 200 && raw < 300)
            return raw - 200; // state input pin
        return -1;
    };

    if (ed::BeginCreate(ImVec4(0.8f, 0.8f, 1.0f, 1.0f), 2.0f))
    {
        ed::PinId startPinId, endPinId;
        if (ed::QueryNewLink(&startPinId, &endPinId))
        {
            if (startPinId && endPinId)
            {
                int fromState = pinToStateOut(startPinId);
                int toState = pinToStateIn(endPinId);

                if (fromState == -1 || toState == -1)
                {
                    fromState = pinToStateOut(endPinId);
                    toState = pinToStateIn(startPinId);
                }

                const int stateCount = static_cast<int>(ui.tree.states.size());
                const bool toValid = (toState >= 0 && toState < stateCount);
                const bool fromValid = (fromState == -2) ||
                                       (fromState >= 0 && fromState < stateCount && fromState != toState);

                if (!toValid || !fromValid)
                {
                    ed::RejectNewItem(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 2.0f);
                }
                else if (ed::AcceptNewItem(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), 2.5f))
                {
                    if (fromState == -2)
                    {
                        ui.tree.entryStateIndex = toState;
                    }
                    else
                    {
                        engine::AnimationTransition transition{};
                        transition.fromStateIndex = fromState;
                        transition.toStateIndex = toState;
                        ui.tree.transitions.push_back(transition);
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

            if (linkRaw == 999)
            {
                // Keep the synthetic entry link stable. Re-routing is handled by
                // creating a new entry connection or via "Set as Entry".
                ed::RejectDeletedItem();
            }
            else if (linkRaw >= 1000)
            {
                if (ed::AcceptDeletedItem())
                {
                    const int tIdx = linkRaw - 1000;
                    if (tIdx >= 0 && tIdx < static_cast<int>(ui.tree.transitions.size()))
                    {
                        ui.tree.transitions.erase(ui.tree.transitions.begin() + tIdx);
                        if (ui.selectedTransitionIndex == tIdx)
                            ui.selectedTransitionIndex = -1;
                        else if (ui.selectedTransitionIndex > tIdx)
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
            if (nodeRaw >= 100)
            {
                const int sIdx = nodeRaw - 100;
                if (ed::AcceptDeletedItem() && sIdx >= 0 && sIdx < static_cast<int>(ui.tree.states.size()))
                {
                    // Remove transitions referencing this state
                    ui.tree.transitions.erase(
                        std::remove_if(ui.tree.transitions.begin(), ui.tree.transitions.end(),
                                       [sIdx](const engine::AnimationTransition &t)
                                       { return t.fromStateIndex == sIdx || t.toStateIndex == sIdx; }),
                        ui.tree.transitions.end());

                    // Adjust indices
                    for (auto &t : ui.tree.transitions)
                    {
                        if (t.fromStateIndex > sIdx)
                            --t.fromStateIndex;
                        if (t.toStateIndex > sIdx)
                            --t.toStateIndex;
                    }
                    if (ui.tree.entryStateIndex > sIdx)
                        --ui.tree.entryStateIndex;
                    else if (ui.tree.entryStateIndex == sIdx)
                        ui.tree.entryStateIndex = 0;

                    ui.tree.states.erase(ui.tree.states.begin() + sIdx);
                    if (ui.tree.stateNodePositions.size() > static_cast<size_t>(sIdx))
                        ui.tree.stateNodePositions.erase(ui.tree.stateNodePositions.begin() + sIdx);

                    ui.dirty = true;
                    ui.selectedTransitionIndex = -1;
                }
            }
        }
    }
    ed::EndDelete();

    // ── Select transition on click ──
    if (ed::GetSelectedObjectCount() > 0)
    {
        ed::LinkId selectedLink;
        if (ed::GetSelectedLinks(&selectedLink, 1) == 1)
        {
            const int raw = static_cast<int>(selectedLink.Get());
            if (raw >= 1000)
                ui.selectedTransitionIndex = raw - 1000;
        }
        else
        {
            ui.selectedTransitionIndex = -1;
        }
    }
    else
    {
        ui.selectedTransitionIndex = -1;
    }

    if (ed::ShowBackgroundContextMenu())
        ImGui::OpenPopup("AnimTreeBGMenu");

    ed::Suspend();
    if (ImGui::BeginPopup("AnimTreeBGMenu"))
    {
        if (ImGui::MenuItem("Add State"))
        {
            ui.addStatePopupOpen = true;
            ui.newStateName[0] = '\0';
            ui.newStateClipPath[0] = '\0';
        }
        ImGui::EndPopup();
    }
    ed::Resume();

    ed::Suspend();
    if (ui.addStatePopupOpen)
        ImGui::OpenPopup("AddStatePopup##animtree");

    if (ImGui::BeginPopupModal("AddStatePopup##animtree", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Name##statename", ui.newStateName, sizeof(ui.newStateName));
        ImGui::InputText("Clip path##stateclip", ui.newStateClipPath, sizeof(ui.newStateClipPath));
        ImGui::TextDisabled("(or drag .anim.elixasset after creating)");

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            {
                const std::string dropped(static_cast<const char *>(payload->Data), payload->DataSize - 1);
                std::strncpy(ui.newStateClipPath, dropped.c_str(), sizeof(ui.newStateClipPath) - 1);
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::Button("Add") && ui.newStateName[0] != '\0')
        {
            engine::AnimationTreeState state{};
            state.name = ui.newStateName;
            state.animationAssetPath = ui.newStateClipPath;
            ui.tree.states.push_back(state);
            ui.tree.stateNodePositions.push_back(glm::vec2(280.0f + static_cast<float>(ui.tree.states.size()) * 220.0f, 80.0f));
            ui.dirty = true;
            ui.addStatePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ui.addStatePopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ed::Resume();

    ed::NodeId ctxNode;
    if (ed::ShowNodeContextMenu(&ctxNode))
    {
        const int nodeRaw = static_cast<int>(ctxNode.Get());
        if (nodeRaw >= 100)
        {
            const int sIdx = nodeRaw - 100;
            if (sIdx >= 0 && sIdx < static_cast<int>(ui.tree.states.size()))
            {
                ImGui::OpenPopup(("StateCtxMenu##" + std::to_string(sIdx)).c_str());
            }
        }
    }

    // State context menus
    for (int i = 0; i < static_cast<int>(ui.tree.states.size()); ++i)
    {
        ed::Suspend();
        drawStateContextMenu(ui, i);
        ed::Resume();
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

void AnimationTreePanel::drawStateContextMenu(AnimTreeUIState &ui, int stateIndex)
{
    const std::string popupId = "StateCtxMenu##" + std::to_string(stateIndex);
    if (!ImGui::BeginPopup(popupId.c_str()))
        return;

    auto &state = ui.tree.states[static_cast<size_t>(stateIndex)];

    if (ImGui::MenuItem("Set as Entry"))
    {
        ui.tree.entryStateIndex = stateIndex;
        ui.dirty = true;
    }

    if (ImGui::MenuItem("Rename"))
    {
        ui.renamePopupOpen = true;
        ui.renameStateIndex = stateIndex;
        std::strncpy(ui.renameBuffer, state.name.c_str(), sizeof(ui.renameBuffer) - 1);
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Delete"))
    {
        // Deletion handled via ed::QueryDeletedNode in drawNodeGraph
        ed::DeleteNode(ed::NodeId(stateNodeId(stateIndex)));
    }

    ImGui::EndPopup();

    // Rename popup
    if (ui.renamePopupOpen && ui.renameStateIndex == stateIndex)
    {
        ImGui::OpenPopup(("RenameState##" + std::to_string(stateIndex)).c_str());
        ui.renamePopupOpen = false;
    }

    if (ImGui::BeginPopupModal(("RenameState##" + std::to_string(stateIndex)).c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Name", ui.renameBuffer, sizeof(ui.renameBuffer));
        if (ImGui::Button("OK") && ui.renameBuffer[0] != '\0')
        {
            ui.tree.states[static_cast<size_t>(stateIndex)].name = ui.renameBuffer;
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
    const int tIdx = ui.selectedTransitionIndex;

    if (tIdx < 0 || tIdx >= static_cast<int>(ui.tree.transitions.size()))
    {
        ImGui::TextDisabled("Click a transition link to inspect it.");
        return;
    }

    auto &t = ui.tree.transitions[static_cast<size_t>(tIdx)];

    const std::string fromName = (t.fromStateIndex == -1) ? "Any"
                                 : (t.fromStateIndex < static_cast<int>(ui.tree.states.size()))
                                     ? ui.tree.states[static_cast<size_t>(t.fromStateIndex)].name
                                     : "?";
    const std::string toName = (t.toStateIndex >= 0 && t.toStateIndex < static_cast<int>(ui.tree.states.size()))
                                   ? ui.tree.states[static_cast<size_t>(t.toStateIndex)].name
                                   : "?";

    ImGui::Text("Transition: %s  ->  %s", fromName.c_str(), toName.c_str());
    ImGui::Separator();

    if (ImGui::DragFloat("Blend Duration (s)", &t.blendDuration, 0.01f, 0.0f, 5.0f, "%.2f"))
        ui.dirty = true;
    if (ImGui::Checkbox("Has Exit Time", &t.hasExitTime))
        ui.dirty = true;
    if (t.hasExitTime)
    {
        if (ImGui::SliderFloat("Exit Time", &t.exitTime, 0.0f, 1.0f))
            ui.dirty = true;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Conditions:");

    int removeCondIdx = -1;
    for (int ci = 0; ci < static_cast<int>(t.conditions.size()); ++ci)
    {
        auto &cond = t.conditions[static_cast<size_t>(ci)];
        ImGui::PushID(ci);

        // Parameter name combo
        const char *curParamName = cond.parameterName.empty() ? "<select>" : cond.parameterName.c_str();
        if (ImGui::BeginCombo("Param##cond", curParamName))
        {
            for (const auto &param : ui.tree.parameters)
            {
                bool sel = (param.name == cond.parameterName);
                if (ImGui::Selectable(param.name.c_str(), sel))
                {
                    cond.parameterName = param.name;
                    // Auto-set condition type based on param type
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

        // Operator
        switch (cond.type)
        {
        case engine::AnimationTransitionCondition::Type::FloatGreater:
        case engine::AnimationTransitionCondition::Type::FloatLess:
        {
            int opIdx = (cond.type == engine::AnimationTransitionCondition::Type::FloatGreater) ? 0 : 1;
            if (ImGui::Combo("##op", &opIdx, "> \0< \0"))
            {
                cond.type = (opIdx == 0) ? engine::AnimationTransitionCondition::Type::FloatGreater
                                         : engine::AnimationTransitionCondition::Type::FloatLess;
                ui.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::DragFloat("##fthresh", &cond.floatThreshold, 0.01f))
                ui.dirty = true;
            break;
        }
        case engine::AnimationTransitionCondition::Type::BoolTrue:
        case engine::AnimationTransitionCondition::Type::BoolFalse:
        {
            int opIdx = (cond.type == engine::AnimationTransitionCondition::Type::BoolTrue) ? 0 : 1;
            if (ImGui::Combo("##boolop", &opIdx, "True\0False\0"))
            {
                cond.type = (opIdx == 0) ? engine::AnimationTransitionCondition::Type::BoolTrue
                                         : engine::AnimationTransitionCondition::Type::BoolFalse;
                ui.dirty = true;
            }
            break;
        }
        case engine::AnimationTransitionCondition::Type::IntEqual:
            if (ImGui::InputInt("##ival", &cond.intValue))
                ui.dirty = true;
            break;
        case engine::AnimationTransitionCondition::Type::Trigger:
            ImGui::TextDisabled("(trigger fires once)");
            break;
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("x##cond"))
            removeCondIdx = ci;

        ImGui::PopID();
    }

    if (removeCondIdx >= 0)
    {
        t.conditions.erase(t.conditions.begin() + removeCondIdx);
        ui.dirty = true;
    }

    if (ImGui::Button("+ Add Condition"))
    {
        engine::AnimationTransitionCondition cond{};
        t.conditions.push_back(cond);
        ui.dirty = true;
    }
}

void AnimationTreePanel::update(float deltaTime)
{
    if (!m_previewPass)
        return;

    bool submittedPreview = false;

    for (auto &[key, ui] : m_uiStates)
    {
        auto &preview = ui.preview;
        if (!preview.active || !preview.animator || !preview.skeletalMesh)
            continue;

        // Sync parameter slider defaults → live animator
        for (const auto &param : ui.tree.parameters)
        {
            switch (param.type)
            {
            case engine::AnimationTreeParameter::Type::Float:
                preview.animator->setFloat(param.name, param.floatDefault);
                break;
            case engine::AnimationTreeParameter::Type::Bool:
                preview.animator->setBool(param.name, param.boolDefault);
                break;
            case engine::AnimationTreeParameter::Type::Int:
                preview.animator->setInt(param.name, param.intDefault);
                break;
            default:
                break;
            }
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
                    if (c.score > bestScore) bestScore = c.score;

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
                    entry.gpuMesh      = std::move(gpu);
                    entry.material     = entry.gpuMesh->material
                                             ? entry.gpuMesh->material
                                             : engine::Material::getDefaultMaterial();
                    entry.sourceMesh   = cpuMeshes[candidate.slot];
                    entry.localTransform  = candidate.previewMesh.localTransform;
                    entry.attachedBoneId  = candidate.previewMesh.attachedBoneId;
                    entry.skinned      = candidate.skinned;
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
                        entry.gpuMesh      = std::move(gpu);
                        entry.material     = entry.gpuMesh->material
                                                 ? entry.gpuMesh->material
                                                 : engine::Material::getDefaultMaterial();
                        entry.sourceMesh   = cpuMeshes[candidate.slot];
                        entry.localTransform  = candidate.previewMesh.localTransform;
                        entry.attachedBoneId  = candidate.previewMesh.attachedBoneId;
                        entry.skinned      = candidate.skinned;
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

            if (preview.isFirstActivation)
            {
                resetPreviewCamera(preview);
                preview.isFirstActivation = false;
            }
        }

        preview.animator->update(deltaTime);

        // Build draw data from cached GPU meshes
        AnimPreviewDrawData data{};
        data.modelMatrix = preview.modelMatrix;
        data.viewMatrix = buildOrbitView(preview);
        data.projMatrix = buildOrbitProj();

        const auto &finalBones = preview.skeletalMesh->getSkeleton().getFinalMatrices();

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
                if (auto *attachmentBone = preview.skeletalMesh->getSkeleton().getBone(previewMesh.attachedBoneId))
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

    if (!submittedPreview)
        m_previewPass->setPreviewData(AnimPreviewDrawData{});
}

void AnimationTreePanel::drawPreviewPane(AnimTreeUIState &ui)
{
    ImGui::Separator();
    ImGui::TextDisabled("Preview");

    auto &preview = ui.preview;

    if (!preview.active || !preview.animator)
    {
        ImGui::TextDisabled("Open this tree via an entity's AnimatorComponent to enable preview.");
        return;
    }

    const float avail = ImGui::GetContentRegionAvail().x;
    // Use the available height minus a small footer (Reset Camera button + state text + spacing)
    const float footerH = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f;
    const float previewH = glm::max(ImGui::GetContentRegionAvail().y - footerH, avail * 0.6f);
    const ImVec2 previewSize(avail, previewH);

    refreshLivePreviewDescriptor();

    const VkDescriptorSet previewDescriptor =
        (m_livePreviewDescriptorSet != VK_NULL_HANDLE) ? m_livePreviewDescriptorSet : m_sharedPreviewDescriptorSet;
    if (previewDescriptor != VK_NULL_HANDLE)
    {
        ImGui::Image((ImTextureID)(uintptr_t)previewDescriptor, previewSize);

        if (ImGui::IsItemHovered())
        {
            // Orbit with left mouse drag
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                preview.yaw -= delta.x * 0.5f;
                preview.pitch -= delta.y * 0.5f;
                preview.pitch = glm::clamp(preview.pitch, -89.0f, 89.0f);
            }

            // Zoom with scroll
            const float scroll = ImGui::GetIO().MouseWheel;
            if (scroll != 0.0f)
                preview.distance = glm::clamp(preview.distance - scroll * 0.3f, 0.5f, 20.0f);
        }
    }
    else
    {
        ImGui::TextDisabled("(preview not ready)");
    }

    if (ImGui::SmallButton("Reset Camera"))
        resetPreviewCamera(preview);

    // State info
    const std::string stateName = preview.animator->getCurrentStateName();
    ImGui::Text("State: %s", stateName.empty() ? "(none)" : stateName.c_str());
    if (preview.animator->isInTransition())
        ImGui::TextDisabled("  -> %.0f%%", preview.animator->getCurrentStateNormalizedTime() * 100.0f);
}

void AnimationTreePanel::resetPreviewCamera(AnimPreviewContext &ctx)
{
    ctx.target = glm::vec3(0.0f);
    ctx.yaw = 15.0f;
    ctx.pitch = 8.0f;
    ctx.distance = 3.5f;
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
