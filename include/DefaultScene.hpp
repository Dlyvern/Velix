#ifndef DEFAULT_SCENE_HPP
#define DEFAULT_SCENE_HPP

#include "ElixirCore/Scene.hpp"
#include "Skybox.hpp"

class DefaultScene final : public Scene
{
public:
    DefaultScene();

    void create() override;

    void update(float deltaTime) override;

    bool isOver() override;

    ~DefaultScene() override;
private:
    Skybox m_skybox;
};

#endif //DEFAULT_SCENE_HPP