#ifndef ELIX_EDITOR_HPP
#define ELIX_EDITOR_HPP

#include "Core/Macros.hpp"
#include "Core/Window.hpp"
#include "Core/Sampler.hpp"

#include "Engine/Texture.hpp"
#include "Engine/Camera.hpp"
#include "Core/CommandBuffer.hpp"
#include "Editor/Project.hpp"
#include "Editor/Notification.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "TextEditor.h"

#include "Engine/Render/RenderGraphPassPerFrameData.hpp"
#include "Engine/Render/RenderGraph/RenderGraphProfilingData.hpp"

#include "Engine/Scene.hpp"
#include <volk.h>

#include <vector>
#include <optional>
#include <string>

#include "Editor/EditorResourcesStorage.hpp"
#include "Editor/AssetsWindow.hpp"
#include <backends/imgui_impl_vulkan.h>

#include "Editor/AssetsPreviewSystem.hpp"

#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class Editor
{
public:
    enum EditorMode
    {
        EDIT = 0,
        PLAY = 1,
        PAUSE = 2
    };

    Editor();

    void initStyle();

    engine::Camera::SharedPtr getCurrentCamera();

    //! Maybe we can do something better here
    void setScene(engine::Scene::SharedPtr scene)
    {
        m_scene = scene;
    }

    void setProject(const std::shared_ptr<Project> &project)
    {
        m_currentProject = project;
        m_assetsPreviewSystem.setProject(project.get());

        if (m_assetsWindow)
            m_assetsWindow->setProject(project.get());
    }

    void setObjectIdColorImage(const engine::RenderTarget *renderTarget)
    {
        m_objectIdColorImage = renderTarget;
    }

    void drawFrame(VkDescriptorSet viewportDescriptorSet = VK_NULL_HANDLE);
    void updateAnimationPreview(float deltaTime);

    void processPendingObjectSelection();

    uint32_t getSelectedEntityIdForBuffer() const
    {
        if (!m_selectedEntity)
            return 0u;

        return m_selectedEntity->getId() + 1u;
    }

    uint32_t getSelectedMeshSlotForBuffer() const
    {
        if (!m_selectedMeshSlot.has_value())
            return 0u;

        return m_selectedMeshSlot.value() + 1u;
    }

    void setRenderGraphProfilingData(const engine::renderGraph::RenderGraphFrameProfilingData &profilingData)
    {
        m_renderGraphProfilingData = profilingData;
    }

    void addOnViewportChangedCallback(const std::function<void(uint32_t width, uint32_t height)> &function);

    void addOnModeChangedCallback(const std::function<void(EditorMode)> &function);

    uint32_t getViewportX() const
    {
        return m_viewportSizeX;
    }

    uint32_t getViewportY() const
    {
        return m_viewportSizeY;
    }

    std::vector<AssetsPreviewSystem::RenderPreviewJob> getRequestedPreviewJobs()
    {
        return m_assetsPreviewSystem.captureRequestedRenderJobsForSubmission();
    }

    void setDonePreviewJobs(const std::vector<VkImageView> &views)
    {
        if (!m_defaultSampler)
            return;

        m_assetsPreviewSystem.consumeRenderedJobs(views, m_defaultSampler->vk());
    }

    bool consumeShaderReloadRequest()
    {
        const bool requested = m_pendingShaderReloadRequest;
        m_pendingShaderReloadRequest = false;
        return requested;
    }

private:
    bool saveMaterialToDisk(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial);
    bool reloadMaterialFromDisk(const std::filesystem::path &path);
    engine::Texture::SharedPtr ensureProjectTextureLoaded(const std::string &texturePath);

    engine::Material::SharedPtr ensureMaterialLoaded(const std::string &materialPath);
    bool applyMaterialToSelectedEntity(const std::string &materialPath, std::optional<size_t> slot = std::nullopt);
    bool spawnEntityFromModelAsset(const std::string &assetPath);
    void addPrimitiveEntity(const std::string &primitiveName);

    void openMaterialEditor(const std::filesystem::path &path);
    void openTextDocument(const std::filesystem::path &path);
    bool saveOpenDocument();
    bool compileOpenDocumentShader();
    void setDocumentLanguageFromPath(const std::filesystem::path &path);

    void drawMaterialEditors();

    struct OpenMaterialEditor
    {
        std::filesystem::path path;
        bool open = true;
        bool dirty = false;
    };

    std::vector<OpenMaterialEditor> m_openMaterialEditors;

    TextEditor m_textEditor;
    std::filesystem::path m_openDocumentPath;
    std::string m_openDocumentSavedText;
    bool m_showDocumentWindow{false};
    bool m_pendingShaderReloadRequest{false};
    NotificationManager m_notificationManager;

    AssetsPreviewSystem m_assetsPreviewSystem;

    void drawDocument();

    ImGuiID m_centerDockId = 0;
    ImGuiID m_dockSpaceId = 0;
    ImGuiID m_assetsPanelsDockId = 0;
    bool m_lastDockedAssetsVisibility{false};
    bool m_lastDockedTerminalVisibility{false};

    const engine::RenderTarget *m_objectIdColorImage{nullptr};

    core::Buffer::SharedPtr m_entityIdBuffer{nullptr};
    bool m_hasPendingObjectPick{false};
    uint32_t m_pendingPickX{0};
    uint32_t m_pendingPickY{0};

    engine::GPUMesh::SharedPtr m_selectedObjectMesh{nullptr};

    EditorMode m_currentMode{EditorMode::EDIT};

    std::vector<std::function<void(EditorMode)>> m_onModeChangedCallbacks;

    void changeMode(EditorMode mode);

    enum class GuizmoOperation
    {
        TRANSLATE,
        ROTATE,
        SCALE
    };

    GuizmoOperation m_currentGuizmoOperation{GuizmoOperation::TRANSLATE};

    struct MaterialEditorUIState
    {
        bool openTexturePopup = false;
        std::string texturePopupSlot; // "Albedo", "Normal", "ORM", "Emissive"
        char textureFilter[128] = "";
    };

    std::unordered_map<std::string, MaterialEditorUIState> m_materialEditorUiState;

    void handleInput();

    void setSelectedEntity(engine::Entity *entity);
    void focusSelectedEntity();

    void drawGuizmo();

    float m_movementSpeed{3.0f};

    float m_mouseSensitivity{0.1f};
    bool m_firstClick{true};
    bool m_isViewportMouseCaptured{false};
    double m_capturedMouseRestoreX{0.0};
    double m_capturedMouseRestoreY{0.0};
    bool m_isViewportRightMousePendingContext{false};
    double m_viewportRightMouseDownTime{0.0};
    float m_viewportRightMouseDownX{0.0f};
    float m_viewportRightMouseDownY{0.0f};

    bool m_showAssetsWindow{false};
    bool m_showTerminal{false};
    bool m_terminalAutoScroll{true};
    bool m_terminalClearInputOnSubmit{true};
    char m_terminalCommandBuffer[512]{};
    int m_terminalSelectedLayerMask{(1 << 5) - 1};
    size_t m_terminalLastLogCount{0};
    bool m_forceTerminalScrollToBottom{false};

    engine::Camera::SharedPtr m_editorCamera{nullptr};

    std::weak_ptr<Project> m_currentProject;

    EditorResourcesStorage m_resourceStorage;
    std::shared_ptr<AssetsWindow> m_assetsWindow{nullptr};

    void drawBottomPanel();
    void drawToolBar();
    void showDockSpace();
    void syncAssetsAndTerminalDocking();
    void drawCustomTitleBar();
    void drawAssets();
    void drawTerminal();
    void drawDetails();
    void drawViewport(VkDescriptorSet viewportDescriptorSet);
    void drawHierarchy();
    engine::Scene::SharedPtr m_scene{nullptr};
    engine::Entity *m_selectedEntity{nullptr};
    std::optional<uint32_t> m_selectedMeshSlot;
    engine::Entity *m_cacheEntity{nullptr};
    bool m_isDockingWindowFullscreen{true};

    std::vector<std::function<void(uint32_t width, uint32_t height)>> m_onViewportWindowResized{nullptr};

    uint32_t m_viewportSizeX{0};
    uint32_t m_viewportSizeY{0};

    core::Sampler::SharedPtr m_defaultSampler{nullptr};

    bool m_shotTexturePopup{false};

    engine::renderGraph::RenderGraphFrameProfilingData m_renderGraphProfilingData;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_HPP
