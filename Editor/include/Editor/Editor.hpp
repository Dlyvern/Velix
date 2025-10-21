#ifndef ELIX_EDITOR_HPP
#define ELIX_EDITOR_HPP

#include "Core/Macros.hpp"
#include "Core/Window.hpp"
#include "Engine/TextureImage.hpp"
#include "Core/CommandBuffer.hpp"

#include "Engine/Scene.hpp"
#include <volk.h>

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

struct Project
{
    std::string directory;
};

class Editor
{
public:
    Editor();

    void initStyle();
    //!Maybe we can do something better here
    void setScene(engine::Scene::SharedPtr scene)
    {
        m_scene = scene;
    }

    void drawFrame(VkDescriptorSet viewportDescriptorSet = VK_NULL_HANDLE);
private:
    engine::TextureImage::SharedPtr m_logoTexture{nullptr};
    VkDescriptorSet m_logoDescriptorSet{VK_NULL_HANDLE};

    Project m_currentProject;

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
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_EDITOR_HPP