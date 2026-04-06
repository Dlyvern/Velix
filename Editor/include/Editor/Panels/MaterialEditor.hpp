#ifndef ELIX_MATERIAL_EDITOR_HPP
#define ELIX_MATERIAL_EDITOR_HPP

#include "Core/Macros.hpp"

#include "Editor/Project.hpp"
#include "Editor/Notification.hpp"

#include "TextEditor.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <functional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace ax
{
    namespace NodeEditor
    {
        struct EditorContext;
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class AssetsPreviewSystem;

class MaterialEditor
{
public:
    MaterialEditor();
    ~MaterialEditor();

    void draw();
    void openMaterialEditor(const std::filesystem::path &path);
    void closeMaterialEditor(const std::filesystem::path &path);
    void renameOpenMaterialEditor(const std::filesystem::path &oldPath, const std::filesystem::path &newPath);
    [[nodiscard]] bool hasOpenEditor() const;

    void setProject(Project *project);
    void setNotificationManager(NotificationManager *notificationManager);
    void setCenterDockId(ImGuiID dockId);
    void setRequestedFocusWindowId(ImGuiID windowId) { m_requestedFocusWindowId = windowId; }
    void setAssetsPreviewSystem(AssetsPreviewSystem *assetsPreviewSystem);
    void setSaveMaterialToDiskFunction(const std::function<bool(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial)> &function);
    void setReloadMaterialFromDiskFunction(const std::function<bool(const std::filesystem::path &path)> &function);
    void setEnsureProjectTextureLoadedFunction(const std::function<engine::Texture::SharedPtr(const std::string &texturePath, TextureUsage usage)> &function);
    void setEnsureProjectTextureLoadedPreviewFunction(const std::function<engine::Texture::SharedPtr(const std::string &texturePath, TextureUsage usage)> &function);

private:
    std::function<bool(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial)> m_saveMaterialToDisk{nullptr};
    std::function<bool(const std::filesystem::path &path)> m_reloadMaterialFromDisk{nullptr};
    std::function<engine::Texture::SharedPtr(const std::string &texturePath, TextureUsage usage)> m_ensureProjectTextureLoaded{nullptr};
    std::function<engine::Texture::SharedPtr(const std::string &texturePath, TextureUsage usage)> m_ensureProjectTextureLoadedPreview{nullptr};

    bool saveMaterialToDisk(const std::filesystem::path &path, const engine::CPUMaterial &cpuMaterial);
    bool reloadMaterialFromDisk(const std::filesystem::path &path);
    engine::Texture::SharedPtr ensureProjectTextureLoaded(const std::string &texturePath, TextureUsage usage = TextureUsage::Color);
    engine::Texture::SharedPtr ensureProjectTextureLoadedPreview(const std::string &texturePath, TextureUsage usage = TextureUsage::Color);
    void destroyNodeEditorStateForPath(const std::filesystem::path &materialPath);
    void closeCurrentMaterialEditor();

    struct OpenMaterialEditor
    {
        std::filesystem::path path;
        bool open = false;
        bool dirty = false;
        bool confirmCloseRequested = false;
    };

    struct MaterialEditorUIState
    {
        struct DynamicColorNode
        {
            int nodeId = 0;
            int outputPinId = 0;
            int linkId = 0;
            engine::CPUMaterial::ColorNodeParams params;
            glm::vec2 spawnPosition{0.0f, 0.0f};
            bool pendingPlacement = false;
            bool removeRequested = false;
        };

        bool openTexturePopup = false;
        std::string texturePopupSlot; // "Albedo", "Normal", "ORM", "Emissive"
        char textureFilter[128] = "";
        bool openColorPopup = false;
        int colorPopupSlot = 0; // 0-none, 1-baseColor, 2-emissive

        bool nodeEditorInitialized = false;
        ax::NodeEditor::EditorContext *nodeEditorContext = nullptr;

        int mappingNodeId = 1;
        int texturesNodeId = 2;
        int outputNodeId = 3;

        int mappingOutVectorPinId = 11;
        int texturesInVectorPinId = 12;

        int texturesOutAlbedoPinId = 21;
        int texturesOutNormalPinId = 22;
        int texturesOutOrmPinId = 23;
        int texturesOutEmissivePinId = 24;

        int outputInAlbedoPinId = 31;
        int outputInNormalPinId = 32;
        int outputInOrmPinId = 33;
        int outputInEmissivePinId = 34;
        int outputInRoughnessPinId = 35;
        int outputInMetallicPinId = 36;
        int outputInAoPinId = 37;
        int outputInAlphaPinId = 38;

        int linkMappingId = 101;
        int linkAlbedoId = 102;
        int linkNormalId = 103;
        int linkOrmId = 104;
        int linkEmissiveId = 105;

        bool customShaderInitialized = false;
        bool customShaderPanelOpen = false;
        TextEditor customFunctionsEditor;
        TextEditor customExpressionEditor;
        std::string customShaderLastError;
        bool customShaderHasError = false;
        bool initialCustomShaderRefreshDone = false;

        int nextDynamicColorNodeId = 5000;
        int nextDynamicColorPinId = 6000;
        int nextDynamicColorLinkId = 7000;
        std::vector<DynamicColorNode> dynamicColorNodes;
        bool colorNodesInitialized = false;

        struct DynamicNoiseNode
        {
            int nodeId = 0;
            int outputPinId = 0;
            int linkId = 0;
            engine::CPUMaterial::NoiseNodeParams params;

            glm::vec2 spawnPosition{0.0f, 0.0f};
            bool pendingPlacement = false;
            bool removeRequested = false;
        };

        int nextNoiseNodeId  = 8000;
        int nextNoisePinId   = 9000;
        int nextNoiseLinkId  = 10000;
        std::vector<DynamicNoiseNode> dynamicNoiseNodes;
        bool noiseNodesInitialized = false;
    };

    void notify(NotificationType type, const std::string &message);

    Project *m_project{nullptr};
    NotificationManager *m_notificationManager{nullptr};
    AssetsPreviewSystem *m_assetsPreviewSystem{nullptr};
    ImGuiID m_centerDockId{0};
    ImGuiID m_requestedFocusWindowId{0};
    std::unordered_map<std::string, MaterialEditorUIState> m_materialEditorUiState;
    OpenMaterialEditor m_currentOpenedMaterialEditor;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MATERIAL_EDITOR_HPP
