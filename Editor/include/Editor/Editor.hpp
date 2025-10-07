#ifndef ELIX_EDITOR_HPP
#define ELIX_EDITOR_HPP

#include "Core/Macros.hpp"
#include "Core/Window.hpp"

#include "Core/CommandBuffer.hpp"

#include "Engine/Scene.hpp"
#include <volk.h>

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class Editor
{
public:
    //!Maybe we can do something better heres
    void setScene(engine::Scene::SharedPtr scene)
    {
        m_scene = scene;
    }
    void drawFrame();
private:
    void drawDetails();
    void drawViewport();
    void drawHierarchy();
    void drawBenchmark();
    engine::Scene::SharedPtr m_scene{nullptr};
    engine::Entity::SharedPtr m_selectedEntity{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_EDITOR_HPP