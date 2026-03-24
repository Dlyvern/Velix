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

    void setProject(Project *project) { m_project = project; }
    void setNotificationManager(NotificationManager *nm) { m_notificationManager = nm; }
    void setCenterDockId(ImGuiID dockId) { m_centerDockId = dockId; }
    void setSaveTreeFunction(std::function<bool(const std::filesystem::path &, const engine::AnimationTree &)> fn)
    {
        m_saveTreeFunction = std::move(fn);
    }
    void setPreviewPass(AnimationTreePreviewPass *pass) { m_previewPass = pass; }
    void setPreviewDescriptorSet(VkDescriptorSet ds)    { m_sharedPreviewDescriptorSet = ds; }

    ~AnimationTreePanel();

private:
    // ---- Node ID helpers ----
    // Entry node: NodeId=1, output pin: PinId=2
    // State nodes: NodeId = 100+idx, input pin = 200+idx, output pin = 300+idx
    // Transition links: LinkId = 1000+idx
    static int stateNodeId(int idx)   { return 100 + idx; }
    static int stateInPin(int idx)    { return 200 + idx; }
    static int stateOutPin(int idx)   { return 300 + idx; }
    static int transitionLinkId(int idx) { return 1000 + idx; }

    struct AnimTreeUIState
    {
        ax::NodeEditor::EditorContext *nodeEditorContext{nullptr};
        bool initialized{false};
        engine::AnimationTree tree;
        bool dirty{false};

        // Selection
        int selectedTransitionIndex{-1};

        // "Add State" popup
        bool addStatePopupOpen{false};
        char newStateName[128]{};
        char newStateClipPath[512]{};

        // "Rename State" popup
        bool renamePopupOpen{false};
        int  renameStateIndex{-1};
        char renameBuffer[128]{};

        // "Add Parameter" popup
        bool addParamPopupOpen{false};
        char newParamName[128]{};
        int  newParamType{0}; // 0=Float,1=Bool,2=Int,3=Trigger

        // Live preview
        AnimPreviewContext preview{};
    };

    struct OpenTreeEditor
    {
        std::filesystem::path path;
        bool open{true};
        bool confirmCloseRequested{false};
    };

    void drawSingleEditor(OpenTreeEditor &editor);
    void drawNodeGraph(AnimTreeUIState &ui, const std::filesystem::path &path);
    void drawParametersPanel(AnimTreeUIState &ui);
    void drawTransitionInspector(AnimTreeUIState &ui);
    void drawStateContextMenu(AnimTreeUIState &ui, int stateIndex);
    void drawPreviewPane(AnimTreeUIState &ui);
    void resetPreviewCamera(AnimPreviewContext &ctx);

    glm::mat4 buildOrbitView(const AnimPreviewContext &ctx) const;
    glm::mat4 buildOrbitProj() const;

    std::unordered_map<std::string, AnimTreeUIState> m_uiStates;
    std::vector<OpenTreeEditor> m_openEditors;

    Project *m_project{nullptr};
    NotificationManager *m_notificationManager{nullptr};
    ImGuiID m_centerDockId{0};
    std::function<bool(const std::filesystem::path &, const engine::AnimationTree &)> m_saveTreeFunction;

    AnimationTreePreviewPass *m_previewPass{nullptr};
    VkDescriptorSet           m_sharedPreviewDescriptorSet{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATION_TREE_PANEL_HPP
