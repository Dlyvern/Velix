#ifndef ELIX_EDITOR_HPP
#define ELIX_EDITOR_HPP

#include "Core/Macros.hpp"
#include "Core/Window.hpp"
#include "Engine/Texture.hpp"
#include "Core/CommandBuffer.hpp"
#include "Engine/Project.hpp"

#include "Engine/Scene.hpp"
#include <volk.h>

#include <vector>

#include "Editor/EditorResourcesStorage.hpp"
#include "Editor/AssetsWindow.hpp"
#include "Engine/EngineCamera.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class Editor
{
public:
    Editor();

    void initStyle();

    void setCamera(std::shared_ptr<engine::EngineCamera> engineCamera)
    {
        m_engineCamera = engineCamera;
    }
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

private:
    enum class GuizmoOperation
    {
        TRANSLATE,
        ROTATE,
        SCALE
    };

    GuizmoOperation m_currentGuizmoOperation{GuizmoOperation::TRANSLATE};

    void handleInput();

    void drawGuizmo();

    bool m_showAssetsWindow{true};

    // engine::Texture::SharedPtr m_logoTexture{nullptr};
    // VkDescriptorSet m_logoDescriptorSet{VK_NULL_HANDLE};

    // engine::Texture::SharedPtr m_folderTexture{nullptr};
    // VkDescriptorSet m_folderDescriptorSet{VK_NULL_HANDLE};

    // engine::Texture::SharedPtr m_fileTexture{nullptr};
    // VkDescriptorSet m_fileDescriptorSet{VK_NULL_HANDLE};

    std::weak_ptr<engine::Project> m_currentProject;

    std::shared_ptr<engine::EngineCamera> m_engineCamera{nullptr};

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