#ifndef ELIX_VELIX_API_HPP
#define ELIX_VELIX_API_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <string>

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

// --- Scene / window ---
void setActiveScene(Scene *scene);
Scene *const getActiveScene();

void setActiveWindow(platform::Window *window);
platform::Window *const getActiveWindow();

// --- Per-frame tick (call after pollEvents, before runtime->tick()) ---
void beginFrame(float deltaTime);

// --- Entities ---
Entity *spawnEntity(const char *name, Scene *scene = nullptr);
bool destroyEntity(Entity *entity, Scene *scene = nullptr);
Entity *findEntityById(uint32_t id, Scene *scene = nullptr);
Entity *findEntityByName(const char *name, Scene *scene = nullptr);
uint64_t getEntitiesCount(Scene *scene = nullptr);
Entity *getEntityByIndex(uint64_t index, Scene *scene = nullptr);

std::string getEntityName(const Entity *entity);

// --- Components ---
ECS *getEntitySingleComponent(Entity *entity, const char *componentTypeName);
uint64_t getEntityComponentsCount(Entity *entity, const char *componentTypeName);
ECS *getEntityComponentByIndex(Entity *entity, const char *componentTypeName, uint64_t index);
bool entityHasComponent(Entity *entity, const char *componentTypeName);

// --- Scene transitions (queued; safe to call during update()) ---
// Full reload: replace the current scene (DontDestroyOnLoad entities survive).
void loadScene(const char *filePath);
// Additive: merge entities from filePath into the current scene.
void loadSceneAdditive(const char *filePath);
// Unload all entities that have the given tag.
void unloadGroup(const char *tag);
// Mark an entity to survive full scene loads.
void setDontDestroyOnLoad(Entity *entity);
// Remove the DontDestroyOnLoad mark.
void clearDontDestroyOnLoad(Entity *entity);

// --- Subsystems ---
InputManager &getInputManager();
RenderQualitySettings *getRenderQualitySettings();
const RenderQualitySettings *getRenderQualitySettingsConst();

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VELIX_API_HPP
