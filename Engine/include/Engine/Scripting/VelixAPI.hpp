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

Entity *spawnEntity(const char *name, Scene *scene = nullptr);
bool destroyEntity(Entity *entity, Scene *scene = nullptr);
Entity *findEntityById(uint32_t id, Scene *scene = nullptr);
uint64_t getEntitiesCount(Scene *scene = nullptr);
Entity *getEntityByIndex(uint64_t index, Scene *scene = nullptr);

std::string getEntityName(const Entity *entity);

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_VELIX_API_HPP
