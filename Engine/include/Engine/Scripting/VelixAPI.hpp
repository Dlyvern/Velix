#ifndef ELIX_VELIX_API_HPP
#define ELIX_VELIX_API_HPP

#include "Core/Macros.hpp"
#include "Engine/Physics/PhysicsScene.hpp"

#include <cstdint>
#include <string>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
class Scene;
ELIX_NESTED_NAMESPACE_END

ELIX_NESTED_NAMESPACE_BEGIN(engine)
class Entity;
class ECS;
class InputManager;
class RenderQualitySettings;
ELIX_NESTED_NAMESPACE_END

ELIX_NESTED_NAMESPACE_BEGIN(platform)
class Window;
ELIX_NESTED_NAMESPACE_END

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(scripting)

void setActiveScene(Scene *scene);
Scene *const getActiveScene();

void setActiveWindow(platform::Window *window);
platform::Window *const getActiveWindow();

void beginFrame(float deltaTime);

Entity *spawnEntity(const char *name, Scene *scene = nullptr);
bool destroyEntity(Entity *entity, Scene *scene = nullptr);
Entity *findEntityById(uint32_t id, Scene *scene = nullptr);
Entity *findEntityByName(const char *name, Scene *scene = nullptr);
uint64_t getEntitiesCount(Scene *scene = nullptr);
Entity *getEntityByIndex(uint64_t index, Scene *scene = nullptr);

std::string getEntityName(const Entity *entity);

ECS *getEntitySingleComponent(Entity *entity, const char *componentTypeName);
uint64_t getEntityComponentsCount(Entity *entity, const char *componentTypeName);
ECS *getEntityComponentByIndex(Entity *entity, const char *componentTypeName, uint64_t index);
bool entityHasComponent(Entity *entity, const char *componentTypeName);

void loadScene(const char *filePath);
void loadSceneAdditive(const char *filePath);
void unloadGroup(const char *tag);
void setDontDestroyOnLoad(Entity *entity);
void clearDontDestroyOnLoad(Entity *entity);

InputManager &getInputManager();
RenderQualitySettings *getRenderQualitySettings();
const RenderQualitySettings *getRenderQualitySettingsConst();

bool raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
             PhysicsRaycastHit *outHit = nullptr, Scene *scene = nullptr);

void addImpulse(Entity *entity, const glm::vec3 &impulse);
void addForce(Entity *entity, const glm::vec3 &force);

glm::vec3 getWorldPosition(Entity *entity);
void setWorldPosition(Entity *entity, const glm::vec3 &position);
glm::quat getWorldRotation(Entity *entity);
void setWorldRotation(Entity *entity, const glm::quat &rotation);
glm::vec3 getForwardVector(Entity *entity);
glm::vec3 getRightVector(Entity *entity);
glm::vec3 getUpVector(Entity *entity);

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VELIX_API_HPP
