#include "Engine/Scene.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"

#include "nlohmann/json.hpp"

#include <fstream>

#include <iostream>

#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Scene::Scene()
{

}

Scene::~Scene()
{

}

const std::vector<Entity::SharedPtr>& Scene::getEntities() const
{
    return m_entities;
}

Entity::SharedPtr Scene::addEntity(const std::string& name)
{
    auto entity = std::make_shared<Entity>(name);

    entity->addComponent<Transform3DComponent>();
    
    m_entities.push_back(entity);
    return entity;
}

void Scene::loadSceneFromFile(const std::string& filePath)
{
    std::ifstream file(filePath);

    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " <<  filePath << std::endl;;
        return;
    }

    nlohmann::json json;

    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error& e)
    {
        std::cerr << "Failed to parse scene file " << e.what() << std::endl;
        return;
    }

    if(json.contains("name"))
    {
        m_name = json["name"];
    }

    if(json.contains("enviroment"))
    {
        for(const auto& enviromentObject : json["enviroment"])
        {
            if(enviromentObject.contains("skybox"))
            {
                //*for now, we do not have skybox
                std::cout << "Has skybox" << std::endl;
            }
        }
    }

    if (json.contains("game_objects"))
    {
        for (const auto& objectJson : json["game_objects"])
        {
            const std::string& name = objectJson.value("name", "undefined");

            auto gameObject = addEntity(name);

            auto transformation = gameObject->getComponent<Transform3DComponent>();

            if (objectJson.contains("position"))
            {
                const auto& pos = objectJson["position"];
                transformation->setPosition({ pos[0], pos[1], pos[2] });
            }

            if (objectJson.contains("scale"))
            {
                const auto& scale = objectJson["scale"];
                transformation->setScale({ scale[0], scale[1], scale[2] });
            }

            if (objectJson.contains("rotation"))
            {
                const auto& rot = objectJson["rotation"];
                // transformation->setRotation({rot[0], rot[1], rot[2] });
            }
        }
    }

    file.close();
}

std::vector<std::shared_ptr<BaseLight>> Scene::getLights() const
{
    std::vector<std::shared_ptr<BaseLight>> lights;

    for(const auto& entity : m_entities)
        if(auto light = entity->getComponent<LightComponent>())
            lights.push_back(light->getLight());

    return lights;
}

void Scene::saveSceneToFile(const std::string& filePath)
{
    nlohmann::json json;

    json["name"] = m_name.empty() ?  std::filesystem::path(filePath).filename().string() : m_name;

    const auto& objects = getEntities();

    for (const auto& object : objects)
    {
        nlohmann::json objectJson;

        objectJson["name"] = object->getName();

        if(const auto& transformation = object->getComponent<Transform3DComponent>())
        {
            objectJson["position"] = {transformation->getPosition().x, transformation->getPosition().y, transformation->getPosition().z};
            objectJson["scale"] = {transformation->getScale().x, transformation->getScale().y, transformation->getScale().z};
            objectJson["rotation"] = {transformation->getRotation().x, transformation->getRotation().y, transformation->getRotation().z};
        }

        json["game_objects"].push_back(objectJson);
    }

    std::ofstream file(filePath);

    if (file.is_open())
    {
        file << std::setw(4) << json << std::endl;
        file.close();
    }
    else
        std::cerr << "Failed to open file to save game objects: " << filePath << std::endl;
}

void Scene::destroyEntity(Entity::SharedPtr entity)
{

}

void Scene::update(float deltaTime)
{
    for(auto& entity : m_entities)
        entity->update(deltaTime);
}

void Scene::fixedUpdate(float fixedDelta)
{
    for(auto& entity : m_entities)
        entity->fixedUpdate(fixedDelta);
}

ELIX_NESTED_NAMESPACE_END