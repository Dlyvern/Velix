#include "Engine/Scripting/VelixAPI.hpp"
#include "Engine/SceneManager.hpp"

#include "Core/Window.hpp"

#include "Engine/Scene.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Input/InputManager.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Time.hpp"

#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/AudioComponent.hpp"
#include "Engine/Components/CameraComponent.hpp"
#include "Engine/Components/CharacterMovementComponent.hpp"
#include "Engine/Components/CollisionComponent.hpp"
#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/ParticleSystemComponent.hpp"
#include "Engine/Components/RigidBodyComponent.hpp"
#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/SpriteMeshComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/Transform2DComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"

#include <glm/gtc/quaternion.hpp>

#include <string_view>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(scripting)

namespace
{
    Scene *g_activeScene{nullptr};
    platform::Window *g_activeWindow{nullptr};

    Scene *resolveScene(Scene *scene)
    {
        return scene ? scene : g_activeScene;
    }

    bool componentNameMatches(std::string_view componentName, std::initializer_list<std::string_view> aliases)
    {
        for (const auto alias : aliases)
            if (componentName == alias)
                return true;

        return false;
    }

    template <typename T>
    void appendSingleComponent(Entity *entity, std::vector<ECS *> &result)
    {
        if (!entity)
            return;

        if (auto *component = entity->getComponent<T>())
            result.push_back(component);
    }

    template <typename T>
    void appendMultiComponents(Entity *entity, std::vector<ECS *> &result)
    {
        if (!entity)
            return;

        const auto components = entity->getComponents<T>();
        for (auto *component : components)
            result.push_back(component);
    }

    std::vector<ECS *> collectComponentsByName(Entity *entity, std::string_view componentTypeName)
    {
        std::vector<ECS *> result;
        if (!entity || componentTypeName.empty())
            return result;

        if (componentNameMatches(componentTypeName, {"Transform3DComponent", "Transform3D", "transform3d", "transform"}))
            appendSingleComponent<Transform3DComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"Transform2DComponent", "Transform2D", "transform2d"}))
            appendSingleComponent<Transform2DComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"CameraComponent", "Camera", "camera"}))
            appendSingleComponent<CameraComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"StaticMeshComponent", "StaticMesh", "static_mesh"}))
            appendSingleComponent<StaticMeshComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"SkeletalMeshComponent", "SkeletalMesh", "skeletal_mesh"}))
            appendSingleComponent<SkeletalMeshComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"AnimatorComponent", "Animator", "animator"}))
            appendSingleComponent<AnimatorComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"LightComponent", "Light", "light"}))
            appendSingleComponent<LightComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"RigidBodyComponent", "RigidBody", "rigidbody"}))
            appendSingleComponent<RigidBodyComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"CollisionComponent", "Collision", "collision"}))
            appendSingleComponent<CollisionComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"CharacterMovementComponent", "CharacterMovement", "character_movement"}))
            appendSingleComponent<CharacterMovementComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"SpriteMeshComponent", "SpriteMesh", "sprite_mesh"}))
            appendSingleComponent<SpriteMeshComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"AudioComponent", "Audio", "audio"}))
            appendMultiComponents<AudioComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"ScriptComponent", "Script", "script"}))
            appendMultiComponents<ScriptComponent>(entity, result);
        else if (componentNameMatches(componentTypeName, {"ParticleSystemComponent", "ParticleSystem", "particle_system"}))
            appendMultiComponents<ParticleSystemComponent>(entity, result);

        return result;
    }
} // namespace

void setActiveScene(Scene *scene)
{
    g_activeScene = scene;
}

Scene *const getActiveScene()
{
    return g_activeScene;
}

void setActiveWindow(platform::Window *window)
{
    g_activeWindow = window;
    InputManager::instance().setWindow(window ? window->getRawHandler() : nullptr);
}

platform::Window *const getActiveWindow()
{
    return g_activeWindow;
}

void beginFrame(float deltaTime)
{
    InputManager::instance().update();
    Time::instance().update(deltaTime);
}

Entity *spawnEntity(const char *name, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene)
        return nullptr;

    const std::string entityName = name ? name : "Entity";
    auto entity = targetScene->addEntity(entityName);

    return entity ? entity.get() : nullptr;
}

bool destroyEntity(Entity *entity, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene || !entity)
        return false;

    return targetScene->destroyEntity(entity);
}

Entity *findEntityById(uint32_t id, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene)
        return nullptr;

    return targetScene->getEntityById(id);
}

Entity *findEntityByName(const char *name, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene || !name)
        return nullptr;

    for (const auto &entity : targetScene->getEntities())
    {
        if (entity && entity->getName() == name)
            return entity.get();
    }

    return nullptr;
}

uint64_t getEntitiesCount(Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene)
        return 0;

    return targetScene->getEntities().size();
}

Entity *getEntityByIndex(uint64_t index, Scene *scene)
{
    auto *targetScene = resolveScene(scene);

    if (!targetScene)
        return nullptr;

    const auto &entities = targetScene->getEntities();
    if (index >= entities.size())
        return nullptr;

    return entities[index] ? entities[index].get() : nullptr;
}

std::string getEntityName(const Entity *entity)
{
    if (!entity)
        return {};

    return entity->getName();
}

ECS *getEntitySingleComponent(Entity *entity, const char *componentTypeName)
{
    if (!componentTypeName)
        return nullptr;

    const auto components = collectComponentsByName(entity, componentTypeName);
    return components.empty() ? nullptr : components.front();
}

uint64_t getEntityComponentsCount(Entity *entity, const char *componentTypeName)
{
    if (!componentTypeName)
        return 0;

    return static_cast<uint64_t>(collectComponentsByName(entity, componentTypeName).size());
}

ECS *getEntityComponentByIndex(Entity *entity, const char *componentTypeName, uint64_t index)
{
    if (!componentTypeName)
        return nullptr;

    const auto components = collectComponentsByName(entity, componentTypeName);
    if (index >= components.size())
        return nullptr;

    return components[index];
}

bool entityHasComponent(Entity *entity, const char *componentTypeName)
{
    return getEntitySingleComponent(entity, componentTypeName) != nullptr;
}

void loadScene(const char *filePath)
{
    if (filePath)
        SceneManager::instance().requestLoadScene(filePath);
}

void loadSceneAdditive(const char *filePath)
{
    if (filePath)
        SceneManager::instance().requestLoadAdditive(filePath);
}

void unloadGroup(const char *tag)
{
    if (tag)
        SceneManager::instance().requestUnloadGroup(tag);
}

void setDontDestroyOnLoad(Entity *entity)
{
    SceneManager::instance().setDontDestroyOnLoad(entity);
}

void clearDontDestroyOnLoad(Entity *entity)
{
    SceneManager::instance().clearDontDestroyOnLoad(entity);
}

InputManager &getInputManager()
{
    return InputManager::instance();
}

RenderQualitySettings *getRenderQualitySettings()
{
    return &RenderQualitySettings::getInstance();
}

const RenderQualitySettings *getRenderQualitySettingsConst()
{
    return &RenderQualitySettings::getInstance();
}

bool raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
             PhysicsRaycastHit *outHit, Scene *scene)
{
    auto *targetScene = resolveScene(scene);
    if (!targetScene)
        return false;

    return targetScene->getPhysicsScene().raycast(origin, direction, maxDistance, outHit);
}

void addImpulse(Entity *entity, const glm::vec3 &impulse)
{
    if (!entity)
        return;

    if (auto *rb = entity->getComponent<RigidBodyComponent>())
        rb->applyImpulse(impulse);
}

void addForce(Entity *entity, const glm::vec3 &force)
{
    if (!entity)
        return;

    if (auto *rb = entity->getComponent<RigidBodyComponent>())
        rb->addForce(force);
}

glm::vec3 getWorldPosition(Entity *entity)
{
    if (!entity)
        return glm::vec3{0.0f};

    if (auto *t = entity->getComponent<Transform3DComponent>())
        return t->getWorldPosition();

    return glm::vec3{0.0f};
}

void setWorldPosition(Entity *entity, const glm::vec3 &position)
{
    if (!entity)
        return;

    if (auto *t = entity->getComponent<Transform3DComponent>())
        t->setWorldPosition(position);
}

glm::quat getWorldRotation(Entity *entity)
{
    if (!entity)
        return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};

    if (auto *t = entity->getComponent<Transform3DComponent>())
        return t->getWorldRotation();

    return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
}

void setWorldRotation(Entity *entity, const glm::quat &rotation)
{
    if (!entity)
        return;

    if (auto *t = entity->getComponent<Transform3DComponent>())
        t->setWorldRotation(rotation);
}

glm::vec3 getForwardVector(Entity *entity)
{
    if (!entity)
        return glm::vec3{0.0f, 0.0f, -1.0f};

    if (auto *t = entity->getComponent<Transform3DComponent>())
        return glm::normalize(t->getWorldRotation() * glm::vec3{0.0f, 0.0f, -1.0f});

    return glm::vec3{0.0f, 0.0f, -1.0f};
}

glm::vec3 getRightVector(Entity *entity)
{
    if (!entity)
        return glm::vec3{1.0f, 0.0f, 0.0f};

    if (auto *t = entity->getComponent<Transform3DComponent>())
        return glm::normalize(t->getWorldRotation() * glm::vec3{1.0f, 0.0f, 0.0f});

    return glm::vec3{1.0f, 0.0f, 0.0f};
}

glm::vec3 getUpVector(Entity *entity)
{
    if (!entity)
        return glm::vec3{0.0f, 1.0f, 0.0f};

    if (auto *t = entity->getComponent<Transform3DComponent>())
        return glm::normalize(t->getWorldRotation() * glm::vec3{0.0f, 1.0f, 0.0f});

    return glm::vec3{0.0f, 1.0f, 0.0f};
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
