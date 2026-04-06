#include "VelixSDK/VXObject.hpp"

#include "Engine/Components/LightComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

namespace
{
template <typename Fn>
bool withMeshComponent(engine::Entity *entity, Fn &&fn)
{
    if (!entity)
        return false;

    if (auto *staticMesh = entity->getComponent<engine::StaticMeshComponent>())
        return fn(*staticMesh);

    if (auto *skeletalMesh = entity->getComponent<engine::SkeletalMeshComponent>())
        return fn(*skeletalMesh);

    return false;
}
} // namespace

void VXObject::setEnabled(bool enabled)
{
    if (auto *entity = getOuter())
        entity->setEnabled(enabled);
}

bool VXObject::isEnabled() const
{
    if (auto *entity = getOuter())
        return entity->isEnabled();

    return false;
}

bool VXObject::setMeshMaterialOverridePath(std::size_t slot, const std::string &materialPath)
{
    return withMeshComponent(getOuter(), [&](auto &meshComponent)
    {
        if (slot >= meshComponent.getMaterialSlotCount())
            return false;

        meshComponent.setMaterialOverridePath(slot, materialPath);
        return true;
    });
}

bool VXObject::clearMeshMaterialOverride(std::size_t slot)
{
    return withMeshComponent(getOuter(), [&](auto &meshComponent)
    {
        if (slot >= meshComponent.getMaterialSlotCount())
            return false;

        meshComponent.clearMaterialOverride(slot);
        return true;
    });
}

std::size_t VXObject::getMeshMaterialSlotCount() const
{
    std::size_t slotCount = 0;

    withMeshComponent(getOuter(), [&](auto &meshComponent)
    {
        slotCount = meshComponent.getMaterialSlotCount();
        return true;
    });

    return slotCount;
}

bool VXObject::setLightCastsShadows(bool castsShadows)
{
    auto *entity = getOuter();
    if (!entity)
        return false;

    auto *lightComponent = entity->getComponent<engine::LightComponent>();
    if (!lightComponent)
        return false;

    auto light = lightComponent->getLight();
    if (!light)
        return false;

    light->castsShadows = castsShadows;
    return true;
}

bool VXObject::getLightCastsShadows() const
{
    auto *entity = getOuter();
    if (!entity)
        return false;

    auto *lightComponent = entity->getComponent<engine::LightComponent>();
    if (!lightComponent)
        return false;

    auto light = lightComponent->getLight();
    return light ? light->castsShadows : false;
}

ELIX_NESTED_NAMESPACE_END
