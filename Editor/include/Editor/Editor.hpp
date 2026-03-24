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
#include "Editor/Actions/EditorActionHistory.hpp"
#include "Editor/Panels/MaterialEditor.hpp"
#include "Editor/Panels/AnimationTreePanel.hpp"
#include "Editor/Panels/HierarchyPanel.hpp"
#include "Editor/Panels/DetailsPanel.hpp"
#include "Editor/Panels/TerminalPanel.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "TextEditor.h"

#include "Engine/Render/RenderGraphPassPerFrameData.hpp"
#include "Engine/Render/RenderGraph/RenderGraphProfilingData.hpp"
#include "Engine/Scripting/ScriptsRegister.hpp"

#include "Engine/Scene.hpp"
#include "Engine/Runtime/ProjectConfig.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include <volk.h>

#include <vector>
#include <optional>
#include <string>
#include <cstdint>
#include <cstddef>
#include <functional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Editor/EditorResourcesStorage.hpp"
#include "Editor/AssetsWindow.hpp"
#include <backends/imgui_impl_vulkan.h>

#include "Editor/AssetsPreviewSystem.hpp"
#include "Editor/Terrain/TerrainTools.hpp"

#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class AssetDetailsView;
class EntityDetailsView;

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
    ~Editor();

    void initStyle(bool imguiBackendRecreated = false);

    engine::Camera::SharedPtr getCurrentCamera();

    //! Maybe we can do something better here
    void setScene(engine::Scene::SharedPtr scene);
    void setCurrentScenePath(const std::filesystem::path &path);

    void setProject(const std::shared_ptr<Project> &project)
    {
        engine::ScriptsRegister::setActiveRegister(nullptr);

        if (auto previousProject = m_currentProject.lock(); previousProject && previousProject != project && previousProject->projectLibrary)
        {
            engine::PluginLoader::closeLibrary(previousProject->projectLibrary);
            previousProject->projectLibrary = nullptr;
        }

        m_projectScriptsRegister = nullptr;
        m_loadedGameModulePath.clear();
        m_currentProject = project;
        m_currentScenePath.clear();
        invalidateModelDetailsCache();
        m_assetsPreviewSystem.setProject(project.get());
        m_terrainTools.setProjectRootPath(project ? std::filesystem::path(project->fullPath) : std::filesystem::path{});
        if (m_materialEditor)
            m_materialEditor->setProject(project.get());

        if (m_assetsWindow)
            m_assetsWindow->setProject(project.get());

        restoreSceneMaterialOverrides();

        if (project)
            loadProjectConfig();
    }

    void setProjectScriptsRegister(engine::ScriptsRegister *scriptsRegister, const std::string &modulePath = {})
    {
        m_projectScriptsRegister = scriptsRegister;
        m_loadedGameModulePath = modulePath;
        engine::ScriptsRegister::setActiveRegister(scriptsRegister);
    }

    void setObjectIdColorImage(const engine::RenderTarget *renderTarget)
    {
        m_objectIdColorImage = renderTarget;
    }

    void drawFrame(VkDescriptorSet viewportDescriptorSet = VK_NULL_HANDLE,
                   VkDescriptorSet gameViewportDescriptorSet = VK_NULL_HANDLE,
                   bool hasGameCamera = false);
    void updateAnimationPreview(float deltaTime);

    void setAnimTreePreviewDescriptorSet(VkDescriptorSet ds);
    void setAnimTreePreviewPass(AnimationTreePreviewPass *pass);

    void processPendingObjectSelection();

    using DrawFn = std::function<void(ImDrawList *dl, const ImVec2 &origin, const ImVec2 &size)>;

    void queueViewportDraw(const std::string &windowName, DrawFn fn)
    {
        m_drawQueue[windowName].push_back(std::move(fn));
    }

    ImDrawList *getWindowDrawList()
    {
        return ImGui::GetWindowDrawList();
    }

    uint32_t
    getSelectedEntityIdForBuffer() const
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
    void addOnGameViewportChangedCallback(const std::function<void(uint32_t width, uint32_t height)> &function);

    void addOnModeChangedCallback(const std::function<void(EditorMode)> &function);

    void setOnSceneOpenRequest(const std::function<void(const std::filesystem::path &)> &function)
    {
        if (m_assetsWindow)
            m_assetsWindow->setOnSceneOpenRequest(function);
        m_pendingSceneOpenRequestCallback = function;
    }

    uint32_t getViewportX() const
    {
        return m_viewportSizeX;
    }

    uint32_t getViewportY() const
    {
        return m_viewportSizeY;
    }

    uint32_t getGameViewportX() const
    {
        return m_gameViewportSizeX;
    }

    uint32_t getGameViewportY() const
    {
        return m_gameViewportSizeY;
    }

    bool isGameViewportVisible() const
    {
        return m_isGameViewportVisible;
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

    void setReloadShadersCallback(std::function<size_t(std::vector<std::string> *)> callback)
    {
        m_reloadShadersCallback = std::move(callback);
    }

    void setRenderViewportOnly(bool value)
    {
        m_renderOnlyViewport = value;
    }

    EditorResourcesStorage &getEditorResourceStorage()
    {
        return m_resourceStorage;
    }

    void setDockingFullscreen(bool value)
    {
        m_isDockingWindowFullscreen = value;

        if (value)
            reinitDocking();
    }

    void reinitDocking()
    {
        m_reinitDocking = true;
    }

    void saveProjectConfig();

private:
    friend class AssetDetailsView;
    friend class EntityDetailsView;

    std::unique_ptr<MaterialEditor> m_materialEditor{nullptr};
    std::unique_ptr<AnimationTreePanel> m_animationTreePanel{nullptr};
    HierarchyPanel m_hierarchyPanel;
    DetailsPanel m_detailsPanel;
    TerminalPanel m_terminalPanel;
    std::unordered_map<std::string, std::vector<DrawFn>> m_drawQueue;
    bool m_renderOnlyViewport{false};

    bool saveMaterialToDisk(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial);
    bool reloadMaterialFromDisk(const std::filesystem::path &path);
    engine::Texture::SharedPtr ensureProjectTextureLoaded(const std::string &texturePath, TextureUsage usage = TextureUsage::Color);
    engine::Texture::SharedPtr ensureProjectTextureLoadedPreview(const std::string &texturePath, TextureUsage usage = TextureUsage::Color);
    const engine::ModelAsset *ensureModelAssetLoaded(const std::string &modelPath);
    void invalidateModelDetailsCache();
    void rebuildModelDetailsCache(const engine::ModelAsset &modelAsset,
                                  const std::filesystem::path &modelDirectory,
                                  const std::filesystem::path &projectRoot,
                                  const std::filesystem::path &textureSearchDirectory);
    bool buildPerMeshMaterialPathsFromDirectory(const engine::ModelAsset &modelAsset,
                                                const std::filesystem::path &materialsDirectory,
                                                std::vector<std::string> &outPerMeshMaterialPaths,
                                                size_t &outMatchedSlots) const;
    bool applyPerMeshMaterialPathsToSelectedEntity(const std::vector<std::string> &perMeshMaterialPaths);
    bool exportModelMaterials(
        const std::filesystem::path &modelPath,
        const std::filesystem::path &outputDirectory,
        const std::filesystem::path &textureSearchDirectory,
        const std::unordered_map<std::string, std::string> &textureOverrides,
        std::vector<std::string> *outPerMeshMaterialPaths = nullptr);

    engine::Material::SharedPtr ensureMaterialLoaded(const std::string &materialPath);
    void restoreSceneMaterialOverrides();
    bool applyMaterialToSelectedEntity(
        const std::string &materialPath,
        std::optional<size_t> slot = std::nullopt,
        bool forceAllSlots = false);
    bool spawnEntityFromModelAsset(const std::string &assetPath);
    void addPrimitiveEntity(const std::string &primitiveName);
    void addEmptyEntity(const std::string &name = "Empty");
    void addDefaultCharacterEntity(const std::string &name = "Character");

    void openMaterialEditor(const std::filesystem::path &path);
    void openAnimationTreeEditor(const std::filesystem::path &path,
                                 engine::AnimatorComponent     *animator     = nullptr,
                                 engine::SkeletalMeshComponent *skeletalMesh = nullptr);
    void openTextDocument(const std::filesystem::path &path);
    bool saveOpenDocument();
    bool compileOpenDocumentShader();
    void setDocumentLanguageFromPath(const std::filesystem::path &path);

    void drawMaterialEditors();
    void drawAnimationTreePanels();
    void loadProjectConfig();
    void resetCommandHistory();
    bool executeEditorCommand(std::unique_ptr<actions::IEditorCommand> command);
    bool recordExecutedEditorCommand(std::unique_ptr<actions::IEditorCommand> command);
    bool recordCreatedEntityCommand(engine::Entity *entity, const std::string &label);
    bool performUndoAction();
    bool performRedoAction();
    bool performCopyAction();
    bool performPasteAction();

    TextEditor m_textEditor;
    std::filesystem::path m_openDocumentPath;
    std::string m_openDocumentSavedText;
    bool m_showDocumentWindow{false};
    bool m_documentConfirmCloseRequested{false};
    bool m_pendingShaderReloadRequest{false};
    bool m_openScenePopupRequested{false};
    bool m_showDevTools{false};
    // Callback invoked when "Reload Engine Shaders" is pressed.
    // It should compile all shaders and populate outErrors, returning compiled count.
    std::function<size_t(std::vector<std::string> *)> m_reloadShadersCallback;

    // Results of the last shader reload, displayed in Dev Tools panel.
    int m_devToolsLastCompiledCount{-1};
    std::vector<std::string> m_devToolsShaderErrors;
    NotificationManager m_notificationManager;

    AssetsPreviewSystem m_assetsPreviewSystem;
    TerrainTools m_terrainTools;

    void drawDocument();
    std::filesystem::path resolveCurrentScenePath() const;

    ImGuiID m_centerDockId = 0;
    ImGuiID m_dockSpaceId = 0;
    ImGuiID m_assetsPanelsDockId = 0;
    bool m_lastDockedAssetsVisibility{false};
    bool m_lastDockedTerminalVisibility{false};
    bool m_lastDockedUIToolsVisibility{false};

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

    enum class ColliderHandleType : uint8_t
    {
        NONE = 0,
        BOX_X,
        BOX_Y,
        BOX_Z,
        CAPSULE_RADIUS_Y,
        CAPSULE_RADIUS_Z,
        CAPSULE_HEIGHT
    };

    enum class RightSidebarPanelId : uint8_t
    {
        Hierarchy = 0,
        Details = 1
    };

    void handleInput();
    bool saveCurrentScene(bool showNotification = true, bool autosave = false);
    void resetSceneAutosaveTimer();
    void updateSceneAutosave(float deltaSeconds);
    bool hasUnsavedSceneChanges();
    void buildCurrentProject();
    void exportCurrentProjectPacket();

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
    bool m_isNativeWindowDragActive{false};
    ImVec2 m_nativeWindowDragStartMouse{0.0f, 0.0f};
    int m_nativeWindowDragStartX{0};
    int m_nativeWindowDragStartY{0};

    bool m_showAssetsWindow{false};
    bool m_showTerminal{false};
    bool m_showUITools{false};
    bool m_showTerrainTools{false};
    bool m_showRenderSettings{false};
    bool m_showEditorCameraSettings{false};
    bool m_showBenchmark{false};
    bool m_showHierarchyPanel{true};
    bool m_showDetailsPanel{true};
    bool m_isGameViewportVisible{false};
    float m_rightSidebarSplitRatio{0.5f};

    engine::Camera::SharedPtr m_editorCamera{nullptr};

    engine::ProjectConfig m_projectConfig;
    std::weak_ptr<Project> m_currentProject;
    engine::ScriptsRegister *m_projectScriptsRegister{nullptr};
    std::string m_loadedGameModulePath;

    bool m_showCollisionBounds{true};
    bool m_enableCollisionBoundsEditing{true};
    bool m_isColliderHandleHovered{false};
    bool m_isColliderHandleActive{false};
    ColliderHandleType m_activeColliderHandle{ColliderHandleType::NONE};
    glm::vec2 m_colliderDragStartMouse{0.0f};
    glm::vec2 m_colliderDragAxisScreenDir{1.0f, 0.0f};
    float m_colliderDragWorldPerPixel{0.0f};
    glm::vec3 m_colliderDragStartBoxHalfExtents{0.5f};
    float m_colliderDragStartCapsuleRadius{0.5f};
    float m_colliderDragStartCapsuleHalfHeight{0.5f};

    EditorResourcesStorage m_resourceStorage;
    std::shared_ptr<AssetsWindow> m_assetsWindow{nullptr};
    std::function<void(const std::filesystem::path &)> m_pendingSceneOpenRequestCallback{nullptr};

    void drawBottomPanel();
    void drawToolBar();
    void drawRightSidebar();
    void drawEditorCameraSettings();
    void drawRenderSettings();
    void drawBenchmark();
    void drawDevTools();
    void drawUITools();
    void drawTerrainTools();
    bool hasVisibleRightSidebarPanels() const;
    void persistRightSidebarSettings() const;
    void setRightSidebarPanelVisible(RightSidebarPanelId panelId, bool visible);
    void setRightSidebarSplitRatio(float ratio);
    void showDockSpace();
    void syncAssetsAndTerminalDocking();
    void drawCustomTitleBar();
    void drawAssets();
    void drawViewport(VkDescriptorSet viewportDescriptorSet);
    void drawGameViewport(VkDescriptorSet viewportDescriptorSet, bool hasGameCamera);
    actions::EditorCommandHistory m_commandHistory{};
    actions::EditorEntityClipboard m_entityClipboard{};
    engine::Scene::SharedPtr m_scene{nullptr};
    std::filesystem::path m_currentScenePath;
    float m_sceneAutosaveElapsedSeconds{0.0f};
    engine::Entity *m_selectedEntity{nullptr};
    std::optional<uint32_t> m_selectedMeshSlot;
    std::optional<uint32_t> m_lastScrolledMeshSlot; // tracks last slot we auto-scrolled to

    enum class UIPlacementTool : uint8_t
    {
        None = 0,
        Text = 1,
        Button = 2,
        Billboard = 3
    };

    enum class UISelectionType : uint8_t
    {
        None = 0,
        Text = 1,
        Button = 2,
        Billboard = 3
    };

    UIPlacementTool m_uiPlacementTool{UIPlacementTool::None};
    UISelectionType m_uiSelectionType{UISelectionType::None};
    std::size_t m_selectedUIElementIndex{0};
    bool m_hasSelectedUIElement{false};
    bool m_uiPlacementGridEnabled{false};
    bool m_uiPlacementSnapToGrid{true};
    float m_uiPlacementGridStepNdc{0.1f};
    float m_uiBillboardPlacementDistance{6.0f};

    glm::vec2 viewportPixelToNdc(const ImVec2 &pixelPos, const ImVec2 &imageMin, const ImVec2 &imageMax) const;
    glm::vec2 ndcToViewportPixel(const glm::vec2 &ndcPos, const ImVec2 &imageMin, const ImVec2 &imageMax) const;
    glm::vec2 snapNdcToGrid(const glm::vec2 &ndcPos) const;
    glm::vec3 computeBillboardPlacementWorldPosition(const glm::vec2 &ndcPos) const;
    bool trySelectEditorBillboardAtViewportPosition(const ImVec2 &pixelPos, const ImVec2 &imageMin, const ImVec2 &imageMax);
    bool placeUIElementAtViewportPosition(const ImVec2 &pixelPos, const ImVec2 &imageMin, const ImVec2 &imageMax);
    void clearSelectedUIElement();
    std::filesystem::path m_selectedAssetPath;
    std::filesystem::path m_lastModelDetailsAssetPath;

    struct ModelMaterialOverviewEntry
    {
        engine::CPUMaterial material;
        size_t meshUsageCount{0};
        std::string albedoDisplayPath{"-"};
        std::string normalDisplayPath{"-"};
        std::string ormDisplayPath{"-"};
        std::string emissiveDisplayPath{"-"};
    };

    struct ModelDetailsCache
    {
        size_t totalVertexCount{0};
        size_t totalIndexCount{0};
        bool hasSkeleton{false};
        size_t animationCount{0};
        std::vector<ModelMaterialOverviewEntry> materials;
        std::vector<std::string> unresolvedTexturePaths;
    };

    char m_modelMaterialsExportDirectory[512]{};
    char m_modelMaterialsTextureSearchDirectory[512]{};
    std::unordered_map<std::string, std::string> m_modelTextureManualOverrides;
    std::string m_selectedUnresolvedTexturePath;
    char m_selectedTextureOverrideBuffer[512]{};
    ModelDetailsCache m_modelDetailsCache;
    std::filesystem::path m_modelDetailsCacheAssetPath;
    std::filesystem::path m_modelDetailsCacheSearchDirectory;
    bool m_modelDetailsCacheDirty{true};
    engine::Entity *m_cacheEntity{nullptr};

    enum class DetailsContext : uint8_t
    {
        Entity = 0,
        Asset = 1
    };

    DetailsContext m_detailsContext{DetailsContext::Entity};
    bool m_isDockingWindowFullscreen{true};
    bool m_reinitDocking{true};

    std::vector<std::function<void(uint32_t width, uint32_t height)>> m_onViewportWindowResized{nullptr};
    std::vector<std::function<void(uint32_t width, uint32_t height)>> m_onGameViewportWindowResized{nullptr};

    uint32_t m_viewportSizeX{0};
    uint32_t m_viewportSizeY{0};
    uint32_t m_gameViewportSizeX{0};
    uint32_t m_gameViewportSizeY{0};

    core::Sampler::SharedPtr m_defaultSampler{nullptr};

    bool m_shotTexturePopup{false};

    engine::renderGraph::RenderGraphFrameProfilingData m_renderGraphProfilingData;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_HPP
