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

    void setProject(Project *project);
    void setNotificationManager(NotificationManager *notificationManager);
    void setCenterDockId(ImGuiID dockId);
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
            bool linkToEmissiveActive = false;
            glm::vec3 color{1.0f, 1.0f, 1.0f};
            float strength{1.0f};
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
        int principledNodeId = 3;
        int outputNodeId = 4;
        int colorNodeId = 5;

        int mappingOutVectorPinId = 11;
        int texturesInVectorPinId = 12;

        int texturesOutAlbedoPinId = 21;
        int texturesOutNormalPinId = 22;
        int texturesOutOrmPinId = 23;
        int texturesOutEmissivePinId = 24;

        int principledInAlbedoPinId = 31;
        int principledInNormalPinId = 32;
        int principledInOrmPinId = 33;
        int principledInEmissivePinId = 34;
        int principledOutBsdfPinId = 35;

        int outputInSurfacePinId = 41;
        int colorOutPinId = 51;

        int linkMappingId = 101;
        int linkAlbedoId = 102;
        int linkNormalId = 103;
        int linkOrmId = 104;
        int linkEmissiveId = 105;
        int linkOutputId = 106;
        int linkColorToEmissiveId = 107;

        bool linkMappingActive = true;
        bool linkAlbedoActive = false;
        bool linkNormalActive = false;
        bool linkOrmActive = false;
        bool linkEmissiveActive = false;
        bool linkOutputActive = true;
        bool linkColorToEmissiveActive = false;

        bool customShaderInitialized = false;
        bool customShaderPanelOpen = false;
        TextEditor customFunctionsEditor;
        TextEditor customExpressionEditor;
        std::string customShaderLastError;
        bool customShaderHasError = false;

        glm::vec3 colorNodeValue{1.0f, 1.0f, 1.0f};
        float colorNodeStrength{1.0f};

        int nextDynamicColorNodeId = 5000;
        int nextDynamicColorPinId = 6000;
        int nextDynamicColorLinkId = 7000;
        std::vector<DynamicColorNode> dynamicColorNodes;
    };

    void notify(NotificationType type, const std::string &message);

    Project *m_project{nullptr};
    NotificationManager *m_notificationManager{nullptr};
    AssetsPreviewSystem *m_assetsPreviewSystem{nullptr};
    ImGuiID m_centerDockId{0};
    std::unordered_map<std::string, MaterialEditorUIState> m_materialEditorUiState;
    OpenMaterialEditor m_currentOpenedMaterialEditor;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MATERIAL_EDITOR_HPP
