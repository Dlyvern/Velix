#ifndef ELIX_EDITOR_HPP
#define ELIX_EDITOR_HPP

#include "Core/Macros.hpp"
#include "Core/Window.hpp"
#include "Engine/Texture.hpp"
#include "Engine/Camera.hpp"
#include "Core/CommandBuffer.hpp"
#include "Editor/Project.hpp"
#include "Engine/Render/RenderTarget.hpp"

#include "TextEditor.h"

#include "Engine/Render/RenderGraphPassPerFrameData.hpp"

#include "Engine/Scene.hpp"
#include <volk.h>

#include <vector>

#include "Editor/EditorResourcesStorage.hpp"
#include "Editor/AssetsWindow.hpp"
#include <backends/imgui_impl_vulkan.h>

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

    void setProject(const std::shared_ptr<Project> &project)
    {
        m_currentProject = project;

        if (m_assetsWindow)
            m_assetsWindow->setProject(project.get());
    }

    void setObjectIdColorImage(const engine::RenderTarget *renderTarget)
    {
        m_objectIdColorImage = renderTarget;
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

    std::vector<engine::Material *> getRequestedMaterialPreviewJobs()
    {
        return m_requestedMaterialPreviewJobs;
    }

    void clearMaterialPreviewJobs()
    {
        // m_requestedMaterialPreviewJobs.clear();
    }

    void setDoneMaterialJobs(const std::vector<VkImageView> &views)
    {
        // m_materialPreviewDescriptorSets.clear();

        // m_materialPreviewDescriptorSets.reserve(views.size());

        if (m_materialPreviewDescriptorSets.size() != views.size())
        {
            for (VkImageView view : views)
            {
                VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(
                    m_defaultSampler,
                    view,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

                m_materialPreviewDescriptorSets.push_back(set);
            }

            m_assetsWindow->setDoneMaterialJobs(m_materialPreviewDescriptorSets);
        }

        // m_requestedMaterialPreviewJobs.clear();
    }

private:
    TextEditor m_textEditor;

    void drawDocument();

    ImGuiID m_centerDockId = 0;

    std::vector<engine::Material *> m_requestedMaterialPreviewJobs;
    std::vector<VkDescriptorSet> m_materialPreviewDescriptorSets;

    const engine::RenderTarget *m_objectIdColorImage{nullptr};

    core::Buffer::SharedPtr m_entityIdBuffer{nullptr};

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

    void setSelectedEntity(engine::Entity *entity);

    void drawGuizmo();

    float m_movementSpeed{3.0f};

    float m_mouseSensitivity{0.1f};
    bool m_firstClick{true};

    bool m_showAssetsWindow{false};
    bool m_showTerminal{false};

    engine::Camera::SharedPtr m_editorCamera{nullptr};

    std::weak_ptr<Project> m_currentProject;

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
    engine::Entity *m_selectedEntity{nullptr};
    engine::Entity *m_cacheEntity{nullptr};
    bool m_isDockingWindowFullscreen{true};

    std::vector<std::function<void(float width, float height)>> m_onViewportWindowResized{nullptr};

    float m_viewportSizeX{0.0f};
    float m_viewportSizeY{0.0f};

    VkSampler m_defaultSampler{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_HPP