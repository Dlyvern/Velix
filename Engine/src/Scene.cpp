#include "Engine/Scene.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"

#include "Engine/Mesh.hpp"
#include "Engine/Primitives.hpp"

#include "nlohmann/json.hpp"

#include <fstream>

#include <iostream>

#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Scene::Scene() : m_physicsScene(PhysXCore::getInstance()->getPhysics())
{
}

Scene::~Scene()
{
}

PhysicsScene &Scene::getPhysicsScene()
{
    return m_physicsScene;
}

const std::vector<Entity::SharedPtr> &Scene::getEntities() const
{
    return m_entities;
}

// TODO so fucked up method
Scene::SharedPtr Scene::copy()
{
    auto copyScene = std::make_shared<Scene>();

    for (const auto &entity : m_entities)
    {
        //! Copy components etc(Transformation is the most important)
        auto newEntity = std::make_shared<Entity>(entity->getName());
        copyScene->m_entities.push_back(std::move(newEntity));
    }

    copyScene->m_physicsScene = m_physicsScene;
    copyScene->m_name = m_name;
    return copyScene;
}

Entity *Scene::getEntityById(uint32_t id)
{
    for (const auto &entity : m_entities)
        if (entity->getId() == id)
            return entity.get();

    return nullptr;
}

Entity::SharedPtr Scene::addEntity(const std::string &name)
{
    auto doesEntityNameExist = [this](const std::string &name)
    {
        for (auto &&entity : m_entities)
            if (entity->getName() == name)
                return true;

        return false;
    };

    auto generateUniqueName = [this, doesEntityNameExist](const std::string &baseName)
    {
        int counter = 1;
        std::string newName;

        do
        {
            newName = baseName + "_" + (counter < 10 ? "0" : "") + std::to_string(counter);
            counter++;
        } while (doesEntityNameExist(newName));

        return newName;
    };

    std::string actualName = name;
    if (doesEntityNameExist(name))
        actualName = generateUniqueName(name);

    auto entity = std::make_shared<Entity>(actualName);

    entity->addComponent<Transform3DComponent>();
    entity->setId(m_nextEntityId);
    ++m_nextEntityId;

    m_entities.push_back(entity);
    return entity;
}

bool Scene::loadSceneFromFile(const std::string &filePath)
{
    std::ifstream file(filePath);

    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filePath << std::endl;
        ;
        return false;
    }

    nlohmann::json json;

    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        std::cerr << "Failed to parse scene file " << e.what() << std::endl;
        return false;
    }

    if (json.contains("name"))
    {
        m_name = json["name"];
    }

    if (json.contains("enviroment"))
    {
        for (const auto &enviromentObject : json["enviroment"])
        {
            if (enviromentObject.contains("skybox"))
            {
                //*for now, we do not have skybox
                std::cout << "Has skybox" << std::endl;
            }
        }
    }

    if (json.contains("game_objects"))
    {
        for (const auto &objectJson : json["game_objects"])
        {
            const std::string &name = objectJson.value("name", "undefined");

            auto gameObject = addEntity(name);

            CPUMesh mesh = CPUMesh::build<vertex::Vertex3D>(cube::vertices, cube::indices);
            gameObject->addComponent<StaticMeshComponent>(std::vector<CPUMesh>{mesh});

            auto transformation = gameObject->getComponent<Transform3DComponent>();

            if (objectJson.contains("position"))
            {
                const auto &pos = objectJson["position"];
                transformation->setPosition({pos[0], pos[1], pos[2]});
            }

            if (objectJson.contains("scale"))
            {
                const auto &scale = objectJson["scale"];
                transformation->setScale({scale[0], scale[1], scale[2]});
            }

            if (objectJson.contains("rotation"))
            {
                const auto &rot = objectJson["rotation"];
                transformation->setEulerDegrees({rot[0], rot[1], rot[2]});
            }

            if (objectJson.contains("components"))
            {
                for (const auto &componentJson : objectJson["components"])
                {
                    if (!componentJson.contains("type"))
                        continue;

                    const std::string type = componentJson["type"];

                    if (type == "light")
                    {
                        LightComponent::LightType lightType{LightComponent::LightType::NONE};
                        const std::string stringLightType = componentJson["light_type"];

                        if (stringLightType == "directional")
                            lightType = LightComponent::LightType::DIRECTIONAL;
                        else if (stringLightType == "spot")
                            lightType == LightComponent::LightType::SPOT;
                        else if (stringLightType == "point")
                            lightType = LightComponent::LightType::POINT;

                        if (lightType == LightComponent::LightType::NONE)
                        {
                            std::cerr << "Light type is none\n";
                            continue;
                        }

                        LightComponent *lightComponent = gameObject->addComponent<LightComponent>(lightType);
                        auto light = lightComponent->getLight();

                        if (componentJson.contains("color"))
                        {
                            const auto &color = componentJson["color"];
                            light->color = {color[0], color[1], color[2]};
                        }

                        if (componentJson.contains("position"))
                        {
                            const auto &position = componentJson["position"];
                            light->position = {position[0], position[1], position[2]};
                        }

                        if (componentJson.contains("strength"))
                            light->strength = componentJson["strength"];
                    }
                }
            }
        }
    }

    file.close();

    return true;
}

std::vector<std::shared_ptr<BaseLight>> Scene::getLights() const
{
    std::vector<std::shared_ptr<BaseLight>> lights;

    for (const auto &entity : m_entities)
        if (auto light = entity->getComponent<LightComponent>())
            lights.push_back(light->getLight());

    return lights;
}

void Scene::saveSceneToFile(const std::string &filePath)
{
    nlohmann::json json;

    json["name"] = m_name.empty() ? std::filesystem::path(filePath).filename().string() : m_name;

    const auto &objects = getEntities();

    for (const auto &object : objects)
    {
        nlohmann::json objectJson;

        objectJson["name"] = object->getName();

        if (const auto &transformation = object->getComponent<Transform3DComponent>())
        {
            objectJson["position"] = {transformation->getPosition().x, transformation->getPosition().y, transformation->getPosition().z};
            objectJson["scale"] = {transformation->getScale().x, transformation->getScale().y, transformation->getScale().z};
            objectJson["rotation"] = {transformation->getRotation().x, transformation->getRotation().y, transformation->getRotation().z};
        }

        nlohmann::json componentsJson;

        if (const auto &lightComponent = object->getComponent<LightComponent>())
        {
            nlohmann::json lightJson;

            const auto &light = lightComponent->getLight();

            lightJson["type"] = "light";

            std::string stringLightType;

            switch (lightComponent->getLightType())
            {
            case LightComponent::LightType::DIRECTIONAL:
                stringLightType = "directional";
                break;
            case LightComponent::LightType::SPOT:
                stringLightType = "spot";
                break;
            case LightComponent::LightType::POINT:
                stringLightType = "point";
                break;
            default:
                stringLightType = "undefined";
                break;
            }

            lightJson["light_type"] = stringLightType;
            lightJson["color"] = {light->color.x, light->color.y, light->color.z};
            lightJson["position"] = {light->position.x, light->position.y, light->position.z};
            lightJson["strength"] = light->strength;

            if (lightComponent->getLightType() == LightComponent::LightType::DIRECTIONAL)
            {
                const auto &directionalLight = dynamic_cast<DirectionalLight *>(light.get());

                lightJson["direction"] = {directionalLight->direction.x, directionalLight->direction.y, directionalLight->direction.z};
            }
            else if (lightComponent->getLightType() == LightComponent::LightType::POINT)
            {
                const auto &pointLight = dynamic_cast<PointLight *>(light.get());

                lightJson["radius"] = pointLight->radius;
                lightJson["falloff"] = pointLight->falloff;
            }
            else if (lightComponent->getLightType() == LightComponent::LightType::SPOT)
            {
                const auto &spotLight = dynamic_cast<SpotLight *>(light.get());

                lightJson["direction"] = {spotLight->direction.x, spotLight->direction.y, spotLight->direction.z};
                lightJson["inner_angle"] = spotLight->innerAngle;
                lightJson["outer_angle"] = spotLight->outerAngle;
                lightJson["range"] = spotLight->range;
            }

            componentsJson.push_back(lightJson);
        }

        objectJson["components"] = componentsJson;

        json["game_objects"].push_back(objectJson);
    }

    std::ofstream file(filePath);

    if (file.is_open())
    {
        file << std::setw(4) << json << std::endl;
        file.close();
        std::cout << "Saved scene in " << filePath << '\n';
    }
    else
        std::cerr << "Failed to open file to save game objects: " << filePath << std::endl;
}

bool Scene::destroyEntity(Entity *entity)
{
    auto it = std::find_if(m_entities.begin(), m_entities.end(), [entity](const std::shared_ptr<Entity> &en)
                           { return en.get() == entity; });

    if (it == m_entities.end())
        return false;

    m_entities.erase(it);

    return true;
}

void Scene::update(float deltaTime)
{
    for (auto &entity : m_entities)
        entity->update(deltaTime);

    m_physicsScene.update(deltaTime);
}

void Scene::fixedUpdate(float fixedDelta)
{
    for (auto &entity : m_entities)
        entity->fixedUpdate(fixedDelta);
}

ELIX_NESTED_NAMESPACE_END