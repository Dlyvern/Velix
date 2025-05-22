#include "DefaultScene.hpp"
#include "CameraManager.hpp"
#include "ElixirCore/Filesystem.hpp"
#include "Renderer.hpp"
#include "ElixirCore/SceneManager.hpp"

DefaultScene::DefaultScene() = default;

void DefaultScene::create()
{
    CameraManager::getInstance().getActiveCamera()->setCameraMode(CameraMode::FPS);
    Renderer::instance().initShadows();
    this->setGameObjects(SceneManager::loadObjectsFromFile(filesystem::getMapsFolderPath().string() + "/test_scene.json"));

    m_skybox.init({});
    m_skybox.loadFromHDR(filesystem::getSkyboxesFolderPath().string() + "/nice.hdr");
}

void DefaultScene::update(float deltaTime)
{
    for(const auto& gameObject : this->getGameObjects())
        gameObject->update(deltaTime);

    // m_skybox.render();
}

bool DefaultScene::isOver()
{
    return false;
}

DefaultScene::~DefaultScene() = default;
