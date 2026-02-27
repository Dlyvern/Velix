#include "Engine/Scene.hpp"

#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Components/CollisionComponent.hpp"

#include "Engine/Mesh.hpp"
#include "Engine/Primitives.hpp"

#include "nlohmann/json.hpp"
#include <glm/common.hpp>
#include <glm/gtx/quaternion.hpp>

#include <fstream>

#include <iostream>

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_map>
#include <unordered_set>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    glm::quat worldRotationFromForward(const glm::vec3 &forward)
    {
        constexpr float epsilon = std::numeric_limits<float>::epsilon();

        const float directionLength = glm::length(forward);
        if (directionLength <= epsilon)
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        const glm::vec3 normalizedForward = forward / directionLength;
        return glm::rotation(glm::vec3(0.0f, 0.0f, -1.0f), normalizedForward);
    }
}

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

void Scene::setSkyboxHDRPath(const std::string &path)
{
    m_skyboxHDRPath = path;
}

const std::string &Scene::getSkyboxHDRPath() const
{
    return m_skyboxHDRPath;
}

bool Scene::hasSkyboxHDR() const
{
    return !m_skyboxHDRPath.empty();
}

void Scene::clearSkyboxHDR()
{
    m_skyboxHDRPath.clear();
}

const std::vector<Entity::SharedPtr> &Scene::getEntities() const
{
    return m_entities;
}

Scene::SharedPtr Scene::copy()
{
    auto copyScene = std::make_shared<Scene>();
    std::unordered_map<const Entity *, Entity *> copiedEntities;

    for (const auto &entity : m_entities)
    {
        auto copiedEntity = copyScene->addEntity(*entity.get(), entity->getName());
        copiedEntities[entity.get()] = copiedEntity.get();
    }

    for (const auto &entity : m_entities)
    {
        auto *parent = entity->getParent();
        if (!parent)
            continue;

        auto childIt = copiedEntities.find(entity.get());
        auto parentIt = copiedEntities.find(parent);
        if (childIt == copiedEntities.end() || parentIt == copiedEntities.end())
            continue;

        childIt->second->setParent(parentIt->second);
    }

    copyScene->m_physicsScene = m_physicsScene;
    copyScene->m_name = m_name;
    copyScene->m_nextEntityId = m_nextEntityId;
    copyScene->m_skyboxHDRPath = m_skyboxHDRPath;

    return copyScene;
}

Entity *Scene::getEntityById(uint32_t id)
{
    for (const auto &entity : m_entities)
        if (entity->getId() == id)
            return entity.get();

    return nullptr;
}

bool Scene::doesEntityNameExist(const std::string &name) const
{
    for (auto &&entity : m_entities)
        if (entity->getName() == name)
            return true;

    return false;
}

Entity::SharedPtr Scene::addEntity(Entity &en, const std::string &name)
{
    auto generateUniqueName = [this](const std::string &baseName)
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

    auto entity = std::make_shared<Entity>(en, actualName, m_nextEntityId);
    entity->addComponent<Transform3DComponent>();
    ++m_nextEntityId;

    m_entities.push_back(entity);
    return entity;
}

Entity::SharedPtr Scene::addEntity(const std::string &name)
{
    auto generateUniqueName = [this](const std::string &baseName)
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
    m_entities.clear();
    m_nextEntityId = 0;
    m_skyboxHDRPath.clear();

    std::ifstream file(filePath);

    if (!file.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to open file: " << filePath << std::endl);
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
        VX_ENGINE_ERROR_STREAM("Failed to parse scene file " << e.what() << std::endl);
        return false;
    }

    if (json.contains("name"))
    {
        m_name = json["name"];
    }

    const std::filesystem::path sceneDirectory = std::filesystem::path(filePath).parent_path();
    auto resolveScenePath = [&](const std::string &rawPath) -> std::string
    {
        if (rawPath.empty())
            return {};

        std::filesystem::path path(rawPath);
        if (path.is_relative())
            path = sceneDirectory / path;

        return path.lexically_normal().string();
    };

    if (json.contains("environment") && json["environment"].is_object())
    {
        const std::string skyboxHDR = json["environment"].value("skybox_hdr", std::string{});
        m_skyboxHDRPath = resolveScenePath(skyboxHDR);
    }
    else if (json.contains("enviroment"))
    {
        for (const auto &environmentObject : json["enviroment"])
        {
            if (!environmentObject.is_object())
                continue;

            if (environmentObject.contains("skybox_hdr") && environmentObject["skybox_hdr"].is_string())
            {
                m_skyboxHDRPath = resolveScenePath(environmentObject["skybox_hdr"].get<std::string>());
                break;
            }

            if (environmentObject.contains("skybox") && environmentObject["skybox"].is_string())
            {
                m_skyboxHDRPath = resolveScenePath(environmentObject["skybox"].get<std::string>());
                break;
            }
        }
    }

    if (json.contains("game_objects"))
    {
        std::unordered_map<uint32_t, Entity *> entitiesById;
        std::vector<std::pair<Entity *, uint32_t>> pendingParents;
        std::vector<std::pair<Entity *, glm::vec3>> pendingLightDirections;

        for (const auto &objectJson : json["game_objects"])
        {
            const std::string &name = objectJson.value("name", "undefined");

            auto gameObject = addEntity(name);

            if (objectJson.contains("id"))
            {
                const uint32_t objectId = objectJson["id"];
                gameObject->setId(objectId);
                entitiesById[objectId] = gameObject.get();
                m_nextEntityId = std::max<uint64_t>(m_nextEntityId, static_cast<uint64_t>(objectId) + 1u);
            }
            else
                entitiesById[gameObject->getId()] = gameObject.get();

            gameObject->setEnabled(objectJson.value("enabled", true));

            if (objectJson.contains("parent_id"))
                pendingParents.emplace_back(gameObject.get(), objectJson["parent_id"]);

            const bool shouldCreateLegacyMesh = objectJson.value("has_legacy_mesh", true);
            if (shouldCreateLegacyMesh)
            {
                CPUMesh mesh = CPUMesh::build<vertex::Vertex3D>(cube::vertices, cube::indices);
                mesh.name = "Cube";
                gameObject->addComponent<StaticMeshComponent>(std::vector<CPUMesh>{mesh});
            }

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
                            lightType = LightComponent::LightType::SPOT;
                        else if (stringLightType == "point")
                            lightType = LightComponent::LightType::POINT;

                        if (lightType == LightComponent::LightType::NONE)
                        {
                            VX_ENGINE_ERROR_STREAM("Light type is none\n");
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
                            if (!objectJson.contains("position"))
                                transformation->setPosition({position[0], position[1], position[2]});
                        }

                        if (componentJson.contains("strength"))
                            light->strength = componentJson["strength"];

                        if (componentJson.contains("direction"))
                        {
                            const auto &direction = componentJson["direction"];
                            pendingLightDirections.emplace_back(gameObject.get(), glm::vec3{direction[0], direction[1], direction[2]});
                        }

                        if (lightType == LightComponent::LightType::DIRECTIONAL)
                        {
                            if (auto *directionalLight = dynamic_cast<DirectionalLight *>(light.get()))
                                directionalLight->skyLightEnabled = componentJson.value("sky_light_enabled", true);
                        }
                        else if (lightType == LightComponent::LightType::POINT)
                        {
                            if (auto *pointLight = dynamic_cast<PointLight *>(light.get()))
                            {
                                pointLight->radius = componentJson.value("radius", pointLight->radius);
                                pointLight->falloff = componentJson.value("falloff", pointLight->falloff);
                            }
                        }
                        else if (lightType == LightComponent::LightType::SPOT)
                        {
                            if (auto *spotLight = dynamic_cast<SpotLight *>(light.get()))
                            {
                                spotLight->innerAngle = componentJson.value("inner_angle", spotLight->innerAngle);
                                spotLight->outerAngle = componentJson.value("outer_angle", spotLight->outerAngle);
                                spotLight->range = componentJson.value("range", spotLight->range);
                            }
                        }
                    }
                    else if (type == "collision")
                    {
                        std::string collisionType = componentJson.value("collision_type", "box");
                        std::transform(collisionType.begin(), collisionType.end(), collisionType.begin(), ::tolower);

                        CollisionComponent::ShapeType shapeType = CollisionComponent::ShapeType::BOX;
                        glm::vec3 boxHalfExtents(0.5f);
                        float capsuleRadius = 0.5f;
                        float capsuleHalfHeight = 0.5f;
                        physx::PxShape *shape = nullptr;

                        if (collisionType == "capsule")
                        {
                            shapeType = CollisionComponent::ShapeType::CAPSULE;
                            capsuleRadius = componentJson.value("radius", 0.5f);
                            capsuleHalfHeight = componentJson.value("half_height", 0.5f);
                            capsuleRadius = std::max(capsuleRadius, 0.01f);
                            capsuleHalfHeight = std::max(capsuleHalfHeight, 0.0f);
                            shape = m_physicsScene.createShape(physx::PxCapsuleGeometry(capsuleRadius, capsuleHalfHeight));

                            if (shape)
                            {
                                // PhysX capsule axis is +X by default; rotate it to +Y for editor/game consistency.
                                shape->setLocalPose(physx::PxTransform(physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0f, 0.0f, 1.0f))));
                            }
                        }
                        else
                        {
                            if (componentJson.contains("half_extents") &&
                                componentJson["half_extents"].is_array() &&
                                componentJson["half_extents"].size() == 3)
                            {
                                boxHalfExtents.x = componentJson["half_extents"][0];
                                boxHalfExtents.y = componentJson["half_extents"][1];
                                boxHalfExtents.z = componentJson["half_extents"][2];
                            }

                            boxHalfExtents = glm::max(boxHalfExtents, glm::vec3(0.01f));
                            shape = m_physicsScene.createShape(physx::PxBoxGeometry(boxHalfExtents.x, boxHalfExtents.y, boxHalfExtents.z));
                        }

                        if (!shape)
                        {
                            VX_ENGINE_ERROR_STREAM("Failed to create collision shape while loading scene\n");
                            continue;
                        }

                        if (auto *rigidBodyComponent = gameObject->getComponent<RigidBodyComponent>())
                        {
                            rigidBodyComponent->getRigidActor()->attachShape(*shape);
                            if (auto *dynamicBody = rigidBodyComponent->getRigidActor()->is<physx::PxRigidDynamic>())
                                physx::PxRigidBodyExt::updateMassAndInertia(*dynamicBody, 10.0f);

                            gameObject->addComponent<CollisionComponent>(
                                shape,
                                shapeType,
                                boxHalfExtents,
                                capsuleRadius,
                                capsuleHalfHeight,
                                nullptr);
                        }
                        else
                        {
                            const glm::vec3 worldPosition = transformation->getWorldPosition();
                            const glm::quat worldRotation = transformation->getWorldRotation();
                            auto *staticActor = m_physicsScene.createStatic(
                                physx::PxTransform(
                                    physx::PxVec3(worldPosition.x, worldPosition.y, worldPosition.z),
                                    physx::PxQuat(worldRotation.x, worldRotation.y, worldRotation.z, worldRotation.w)));

                            staticActor->attachShape(*shape);
                            gameObject->addComponent<CollisionComponent>(
                                shape,
                                shapeType,
                                boxHalfExtents,
                                capsuleRadius,
                                capsuleHalfHeight,
                                staticActor);
                        }
                    }
                }
            }
        }

        for (const auto &[child, parentId] : pendingParents)
        {
            auto it = entitiesById.find(parentId);
            if (it == entitiesById.end())
            {
                VX_ENGINE_WARNING_STREAM("Parent entity with id " << parentId << " was not found while loading scene.\n");
                continue;
            }

            if (!child->setParent(it->second))
                VX_ENGINE_WARNING_STREAM("Failed to set parent for entity '" << child->getName() << "' while loading scene.\n");
        }

        for (const auto &[entity, direction] : pendingLightDirections)
        {
            if (!entity)
                continue;

            if (auto *transform = entity->getComponent<Transform3DComponent>())
                transform->setWorldRotation(worldRotationFromForward(direction));

            if (auto *lightComponent = entity->getComponent<LightComponent>())
                lightComponent->syncFromOwnerTransform();
        }
    }

    file.close();

    return true;
}

std::vector<std::shared_ptr<BaseLight>> Scene::getLights()
{
    std::vector<std::shared_ptr<BaseLight>> lights;

    for (const auto &entity : m_entities)
    {
        if (!entity || !entity->isEnabled())
            continue;

        if (auto *lightComponent = entity->getComponent<LightComponent>())
        {
            lightComponent->syncFromOwnerTransform();
            lights.push_back(lightComponent->getLight());
        }
    }

    return lights;
}

void Scene::saveSceneToFile(const std::string &filePath)
{
    nlohmann::json json;

    json["name"] = m_name.empty() ? std::filesystem::path(filePath).filename().string() : m_name;

    if (!m_skyboxHDRPath.empty())
    {
        std::filesystem::path skyboxPath = std::filesystem::path(m_skyboxHDRPath).lexically_normal();
        const std::filesystem::path sceneDirectory = std::filesystem::path(filePath).parent_path();

        std::error_code errorCode;
        if (skyboxPath.is_absolute() && !sceneDirectory.empty())
        {
            const auto relativePath = std::filesystem::relative(skyboxPath, sceneDirectory, errorCode);
            if (!errorCode && !relativePath.empty())
                skyboxPath = relativePath.lexically_normal();
        }

        json["environment"] = {
            {"skybox_hdr", skyboxPath.string()}};
    }

    const auto &objects = getEntities();

    for (const auto &object : objects)
    {
        nlohmann::json objectJson;

        objectJson["name"] = object->getName();
        objectJson["id"] = object->getId();
        objectJson["enabled"] = object->isEnabled();
        objectJson["has_legacy_mesh"] = object->hasComponent<StaticMeshComponent>() || object->hasComponent<SkeletalMeshComponent>();

        if (const auto *parent = object->getParent())
            objectJson["parent_id"] = parent->getId();

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
            lightJson["strength"] = light->strength;

            if (lightComponent->getLightType() == LightComponent::LightType::DIRECTIONAL)
            {
                const auto &directionalLight = dynamic_cast<DirectionalLight *>(light.get());

                lightJson["sky_light_enabled"] = directionalLight->skyLightEnabled;
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

                lightJson["inner_angle"] = spotLight->innerAngle;
                lightJson["outer_angle"] = spotLight->outerAngle;
                lightJson["range"] = spotLight->range;
            }

            componentsJson.push_back(lightJson);
        }

        if (const auto &collisionComponent = object->getComponent<CollisionComponent>())
        {
            nlohmann::json collisionJson;
            collisionJson["type"] = "collision";

            if (collisionComponent->getShapeType() == CollisionComponent::ShapeType::CAPSULE)
            {
                collisionJson["collision_type"] = "capsule";
                collisionJson["radius"] = collisionComponent->getCapsuleRadius();
                collisionJson["half_height"] = collisionComponent->getCapsuleHalfHeight();
            }
            else
            {
                collisionJson["collision_type"] = "box";
                const auto halfExtents = collisionComponent->getBoxHalfExtents();
                collisionJson["half_extents"] = {halfExtents.x, halfExtents.y, halfExtents.z};
            }

            componentsJson.push_back(collisionJson);
        }

        objectJson["components"] = componentsJson;

        json["game_objects"].push_back(objectJson);
    }

    std::ofstream file(filePath);

    if (file.is_open())
    {
        file << std::setw(4) << json << std::endl;
        file.close();
        VX_ENGINE_INFO_STREAM("Saved scene in " << filePath << '\n');
    }
    else
        VX_ENGINE_ERROR_STREAM("Failed to open file to save game objects: " << filePath << std::endl);
}

bool Scene::destroyEntity(Entity *entity)
{
    if (!entity)
        return false;

    auto exists = std::find_if(m_entities.begin(), m_entities.end(), [entity](const std::shared_ptr<Entity> &en)
                               { return en.get() == entity; });
    if (exists == m_entities.end())
        return false;

    std::unordered_set<Entity *> entitiesToDestroy;
    std::vector<Entity *> stack{entity};

    while (!stack.empty())
    {
        Entity *current = stack.back();
        stack.pop_back();

        if (!current || entitiesToDestroy.contains(current))
            continue;

        entitiesToDestroy.insert(current);

        for (auto *child : current->getChildren())
            stack.push_back(child);
    }

    for (auto *current : entitiesToDestroy)
        if (current)
            current->clearParent();

    m_entities.erase(
        std::remove_if(m_entities.begin(), m_entities.end(), [&entitiesToDestroy](const std::shared_ptr<Entity> &en)
                       { return entitiesToDestroy.contains(en.get()); }),
        m_entities.end());

    return true;
}

void Scene::update(float deltaTime)
{
    // Scripts can spawn/destroy entities during update.
    // Iterate by index and copy shared_ptr to avoid iterator/reference invalidation.
    for (size_t index = 0; index < m_entities.size(); ++index)
    {
        auto entity = m_entities[index];
        if (entity && entity->isEnabled())
            entity->update(deltaTime);
    }

    m_physicsScene.update(deltaTime);

    for (size_t index = 0; index < m_entities.size(); ++index)
    {
        auto entity = m_entities[index];
        if (!entity)
            continue;

        if (!entity->isEnabled())
            continue;

        if (auto *rigidBodyComponent = entity->getComponent<RigidBodyComponent>())
            rigidBodyComponent->syncFromPhysics();
    }
}

void Scene::fixedUpdate(float fixedDelta)
{
    for (size_t index = 0; index < m_entities.size(); ++index)
    {
        auto entity = m_entities[index];
        if (entity && entity->isEnabled())
            entity->fixedUpdate(fixedDelta);
    }
}

ELIX_NESTED_NAMESPACE_END
