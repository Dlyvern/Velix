#ifndef ELIX_ANIMATION_TREE_PANEL_HPP
#define ELIX_ANIMATION_TREE_PANEL_HPP

#include "Engine/Animation/AnimationTree.hpp"
#include "Editor/Notification.hpp"
#include "Editor/RenderGraphPasses/AnimationTreePreviewPass.hpp"

#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Mesh.hpp"

#include "imgui.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ax::NodeEditor { struct EditorContext; }

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class Project;

class AnimationTreePanel
{
public:
    struct PreviewMeshEntry
    {
        engine::GPUMesh::SharedPtr   gpuMesh{nullptr};
        engine::Material::SharedPtr  material{nullptr};
        engine::CPUMesh              sourceMesh{};
        std::vector<engine::vertex::Vertex3D> dynamicVertices;
        glm::mat4                    localTransform{1.0f};
        int32_t                      attachedBoneId{-1};
        bool                         skinned{false};
    };

    struct AnimPreviewContext
    {
        engine::AnimatorComponent    *animator{nullptr};
        engine::SkeletalMeshComponent*skeletalMesh{nullptr};
        bool                          active{false};
        bool                          runtimeReady{false};
        bool                          playing{true};
        bool                          poseDirty{true};
        float                         playbackSpeed{1.0f};
        float                         playbackAccumulator{0.0f};
        std::size_t                   syncedTreeHash{0u};
        engine::AnimatorComponent     previewAnimator{};
        engine::Skeleton              previewSkeleton{};
        // Orbit camera
        float yaw{0.0f}, pitch{20.0f}, distance{3.0f};
        glm::vec3 target{0.0f};
        bool isFirstActivation{true};
        glm::mat4 modelMatrix{1.0f};
        // Cached preview meshes and their per-mesh transform metadata.
        std::vector<PreviewMeshEntry> previewMeshes;
    };

    void draw();
    void update(float deltaTime);

    void openTree(const std::filesystem::path &path,
                  engine::AnimatorComponent     *animator     = nullptr,
                  engine::SkeletalMeshComponent *skeletalMesh = nullptr);
    void closeTree(const std::filesystem::path &path);
    void renameOpenTree(const std::filesystem::path &oldPath, const std::filesystem::path &newPath);

    void setProject(Project *project) { m_project = project; }
    void setNotificationManager(NotificationManager *nm) { m_notificationManager = nm; }
    void setCenterDockId(ImGuiID dockId) { m_centerDockId = dockId; }
    void setRequestedFocusWindowId(ImGuiID windowId) { m_requestedFocusWindowId = windowId; }
    void setSaveTreeFunction(std::function<bool(const std::filesystem::path &, const engine::AnimationTree &)> fn)
    {
        m_saveTreeFunction = std::move(fn);
    }
    void setPreviewPass(AnimationTreePreviewPass *pass);
    void setPreviewDescriptorSet(VkDescriptorSet ds);
    bool hasActivePreview() const;
    [[nodiscard]] bool hasOpenEditors() const;
    [[nodiscard]] bool consumesDeleteShortcut() const { return m_hasKeyboardFocus; }

    ~AnimationTreePanel();

private:
    // ---- Node Editor ID helpers ----
    static int entryNodeId(int machineNodeId)      { return 1000000 + machineNodeId * 10 + 1; }
    static int entryOutPin(int machineNodeId)      { return 1000000 + machineNodeId * 10 + 2; }
    static int anyNodeId(int machineNodeId)        { return 2000000 + machineNodeId * 10 + 1; }
    static int anyOutPin(int machineNodeId)        { return 2000000 + machineNodeId * 10 + 2; }
    static int graphNodeId(int nodeId)             { return 3000000 + nodeId; }
    static int graphNodeInPin(int nodeId)          { return 4000000 + nodeId * 2; }
    static int graphNodeOutPin(int nodeId)         { return 4000000 + nodeId * 2 + 1; }
    static int transitionLinkId(int machineNodeId, int transitionIndex) { return 5000000 + machineNodeId * 10000 + transitionIndex; }

    struct AnimTreeUIState
    {
        ax::NodeEditor::EditorContext *nodeEditorContext{nullptr};
        bool initialized{false};
        engine::AnimationTree tree;
        bool dirty{false};

        // Selection
        int currentMachineNodeId{-1};
        int selectedNodeId{-1};
        int selectedTransitionIndex{-1};
        int selectedTransitionMachineNodeId{-1};

        // "Add Node" popup
        bool addNodePopupOpen{false};
        int  newNodeType{1}; // 0=Machine,1=Clip,2=BlendSpace1D
        char newNodeName[128]{};
        char newNodeClipPath[512]{};

        // "Rename Node" popup
        bool renamePopupOpen{false};
        int  renameNodeId{-1};
        char renameBuffer[128]{};

        // "Add Parameter" popup
        bool addParamPopupOpen{false};
        char newParamName[128]{};
        int  newParamType{0}; // 0=Float,1=Bool,2=Int,3=Trigger

        // Live preview
        AnimPreviewContext preview{};

        // Cached list of .anim.elixasset paths found in the project
        std::vector<std::string> availableAnimAssets;
    };

    struct OpenTreeEditor
    {
        std::filesystem::path path;
        bool open{true};
        bool confirmCloseRequested{false};
    };

    void refreshAnimationList(AnimTreeUIState &ui) const;

    void drawSingleEditor(OpenTreeEditor &editor);
    void drawNodeGraph(AnimTreeUIState &ui, const std::filesystem::path &path);
    void drawParametersPanel(AnimTreeUIState &ui);
    void drawTransitionInspector(AnimTreeUIState &ui);
    void drawNodeContextMenu(AnimTreeUIState &ui, int nodeId);
    void drawPreviewPane(AnimTreeUIState &ui);
    void resetPreviewCamera(AnimPreviewContext &ctx);
    void refreshLivePreviewDescriptor();
    void invalidateLivePreviewDescriptor();

    glm::mat4 buildOrbitView(const AnimPreviewContext &ctx) const;
    glm::mat4 buildOrbitProj() const;

    std::unordered_map<std::string, AnimTreeUIState> m_uiStates;
    std::vector<OpenTreeEditor> m_openEditors;

    Project *m_project{nullptr};
    NotificationManager *m_notificationManager{nullptr};
    ImGuiID m_centerDockId{0};
    ImGuiID m_requestedFocusWindowId{0};
    std::function<bool(const std::filesystem::path &, const engine::AnimationTree &)> m_saveTreeFunction;

    AnimationTreePreviewPass *m_previewPass{nullptr};
    VkDescriptorSet           m_sharedPreviewDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet           m_livePreviewDescriptorSet{VK_NULL_HANDLE};
    VkImageView               m_livePreviewImageView{VK_NULL_HANDLE};
    VkSampler                 m_livePreviewSampler{VK_NULL_HANDLE};
    bool                      m_hasKeyboardFocus{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATION_TREE_PANEL_HPP
