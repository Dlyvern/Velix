#include "Engine/SDK/VXActor.hpp"

#include "Engine/Scene.hpp"
#include "Engine/Scripting/VelixAPI.hpp"
#include "Core/Logger.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Transform3DComponent &VXActor::getTransform()
{
    auto *entity = getOwnerEntity();
    auto *t = entity->getComponent<Transform3DComponent>();
    if (!t)
        t = entity->addComponent<Transform3DComponent>();
    return *t;
}

glm::vec3 VXActor::getPosition() const
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        return t->getPosition();
    return glm::vec3{0.0f};
}

void VXActor::setPosition(glm::vec3 pos)
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        t->setPosition(pos);
}

glm::quat VXActor::getRotation() const
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        return t->getRotation();
    return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
}

void VXActor::setRotation(glm::quat rot)
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        t->setRotation(rot);
}

glm::vec3 VXActor::getScale() const
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        return t->getScale();
    return glm::vec3{1.0f};
}

void VXActor::setScale(glm::vec3 scale)
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        t->setScale(scale);
}

glm::vec3 VXActor::getWorldPosition() const
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        return t->getWorldPosition();
    return glm::vec3{0.0f};
}

void VXActor::setWorldPosition(glm::vec3 pos)
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        t->setWorldPosition(pos);
}

glm::quat VXActor::getWorldRotation() const
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        return t->getWorldRotation();
    return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
}

void VXActor::setWorldRotation(glm::quat rot)
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        t->setWorldRotation(rot);
}

void VXActor::translate(glm::vec3 delta)
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
        t->setPosition(t->getPosition() + delta);
}

void VXActor::rotate(float angleDeg, glm::vec3 axis)
{
    if (auto *t = getOwnerEntity()->getComponent<Transform3DComponent>())
    {
        const glm::quat deltaRot = glm::angleAxis(glm::radians(angleDeg), glm::normalize(axis));
        t->setRotation(deltaRot * t->getRotation());
    }
}

glm::vec3 VXActor::getForward() const
{
    return glm::normalize(getWorldRotation() * glm::vec3{0.0f, 0.0f, -1.0f});
}

glm::vec3 VXActor::getRight() const
{
    return glm::normalize(getWorldRotation() * glm::vec3{1.0f, 0.0f, 0.0f});
}

glm::vec3 VXActor::getUp() const
{
    return glm::normalize(getWorldRotation() * glm::vec3{0.0f, 1.0f, 0.0f});
}

Entity *VXActor::findEntityByName(const std::string &name)
{
    return scripting::findEntityByName(name.c_str());
}

void VXActor::destroy()
{
    scripting::destroyEntity(getOwnerEntity());
}

VXActor *VXActor::getParentActor() const
{
    auto *entity = getOwnerEntity();
    if (!entity)
        return nullptr;
    auto *parent = entity->getParent();
    if (!parent)
        return nullptr;
    for (auto *comp : parent->getComponents<ScriptComponent>())
    {
        if (auto *actor = dynamic_cast<VXActor *>(comp->getScript()))
            return actor;
    }
    return nullptr;
}

void VXActor::log(const std::string &msg) const
{
    VX_USER_INFO_STREAM("[" << (getOwnerEntity() ? getOwnerEntity()->getName() : "VXActor") << "] " << msg);
}

void VXActor::logWarning(const std::string &msg) const
{
    VX_USER_WARNING_STREAM("[" << (getOwnerEntity() ? getOwnerEntity()->getName() : "VXActor") << "] " << msg);
}

void VXActor::logError(const std::string &msg) const
{
    VX_USER_ERROR_STREAM("[" << (getOwnerEntity() ? getOwnerEntity()->getName() : "VXActor") << "] " << msg);
}

ELIX_NESTED_NAMESPACE_END