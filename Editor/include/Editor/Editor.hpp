#ifndef ELIX_EDITOR_HPP
#define ELIX_EDITOR_HPP

#include "Core/Macros.hpp"
#include "Core/Window.hpp"
#include "Engine/Texture.hpp"
#include "Engine/Camera.hpp"
#include "Core/CommandBuffer.hpp"
#include "Engine/Project.hpp"

#include "Engine/Render/RenderGraphPassPerFrameData.hpp"

#include "Engine/Scene.hpp"
#include <volk.h>

#include <vector>

#include "Editor/EditorResourcesStorage.hpp"
#include "Editor/AssetsWindow.hpp"

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

    Editor(VkDescriptorPool descriptorPool);

    void initStyle();

    engine::Camera::SharedPtr getCurrentCamera();

    //! Maybe we can do something better here
    void setScene(engine::Scene::SharedPtr scene)
    {
        m_scene = scene;
    }

    void setProject(const std::shared_ptr<engine::Project> &project)
    {
        m_currentProject = project;

        if (m_assetsWindow)
            m_assetsWindow->setProject(project.get());
    }

    void drawFrame(VkDescriptorSet viewportDescriptorSet = VK_NULL_HANDLE);

    void addOnViewportChangedCallback(const std::function<void(float width, float height)> &function);

    void addOnModeChangedCallback(const std::function<void(EditorMode)> &function);

    std::vector<engine::AdditionalPerFrameData> getRenderData();

    float getViewportX() const
    {
        return m_viewportSizeX;
    }

    float getViewportY() const
    {
        return m_viewportSizeY;
    }

private:
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};

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

    void handleInput();

    void drawGuizmo();

    float m_movementSpeed{3.0f};

    float m_mouseSensitivity{0.1f};
    bool m_firstClick{true};

    bool m_showAssetsWindow{false};
    bool m_showTerminal{false};

    engine::Camera::SharedPtr m_editorCamera{nullptr};

    std::weak_ptr<engine::Project> m_currentProject;

    EditorResourcesStorage m_resourceStorage;
    std::shared_ptr<AssetsWindow> m_assetsWindow{nullptr};

    void drawBottomPanel();
    void drawToolBar();
    void showDockSpace();
    void drawCustomTitleBar();
    void drawAssets();
    void drawDetails();
    void drawViewport(VkDescriptorSet viewportDescriptorSet);
    void drawHierarchy();
    engine::Scene::SharedPtr m_scene{nullptr};
    engine::Entity::SharedPtr m_selectedEntity{nullptr};
    bool m_isDockingWindowFullscreen{false};

    std::vector<std::function<void(float width, float height)>> m_onViewportWindowResized{nullptr};

    float m_viewportSizeX{0.0f};
    float m_viewportSizeY{0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_HPP