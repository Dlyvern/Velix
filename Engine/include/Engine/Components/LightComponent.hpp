#ifndef ELIX_LIGHT_COMPONENT_HPP
#define ELIX_LIGHT_COMPONENT_HPP

#include "Engine/Lights.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Entity.hpp"

#include <memory>
#include <stdexcept>
#include <limits>
ELIX_NESTED_NAMESPACE_BEGIN(engine)

class LightComponent : public ECS
{
public:
    enum class LightType : uint8_t
    {
        NONE = 0,
        DIRECTIONAL = 1,
        SPOT = 2,
        POINT = 3
    };

    //?Pass light type here
    explicit LightComponent(LightType lightType)
    {
        if(lightType == LightType::NONE)
            throw std::runtime_error("Unknown light type");
        
        changeLightType(lightType);
    }

    LightType getLightType()
    {
        return m_lightType;
    }

    virtual ~LightComponent() = default;

    std::shared_ptr<BaseLight> changeLightType(LightType lightType)
    {
        std::shared_ptr<BaseLight> newLight;

        switch(lightType)
        {
            case LightType::DIRECTIONAL:
            {
                newLight = std::make_shared<DirectionalLight>();
                break;
            }
            case LightType::POINT:
            {
                newLight = std::make_shared<PointLight>(); 
                break;
            } 
            case LightType::SPOT:
            {
                newLight = std::make_shared<SpotLight>();
                break;
            } 
            default:
                throw std::runtime_error("CAN NOT CHANGE LIGHT TYPE TO NONE");
        }

        // Move parameters to a new light.
        if(m_light)
        {
            newLight->color = m_light->color;
            newLight->position = m_light->position;

            m_light.reset();
        }

        m_light = newLight;
        m_lightType = getLightTypeFromLight(m_light.get());
        syncFromOwnerTransform();

        return m_light;
    }

    std::shared_ptr<BaseLight> getLight()
    {
        syncFromOwnerTransform();
        return m_light;
    }

    void syncFromOwnerTransform()
    {
        if (!m_light)
            return;

        auto *owner = getOwner<Entity>();
        if (!owner)
            return;

        auto *transform = owner->getComponent<Transform3DComponent>();
        if (!transform)
            return;

        m_light->position = transform->getWorldPosition();

        if (auto *directional = dynamic_cast<DirectionalLight *>(m_light.get()))
            directional->direction = computeWorldForward(*transform);
        else if (auto *spot = dynamic_cast<SpotLight *>(m_light.get()))
            spot->direction = computeWorldForward(*transform);
    }

protected:
    void onOwnerAttached() override
    {
        syncFromOwnerTransform();
    }

private:
    static glm::vec3 computeWorldForward(const Transform3DComponent &transform)
    {
        constexpr float epsilon = std::numeric_limits<float>::epsilon();

        const glm::vec3 forward = transform.getWorldRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
        const float forwardLength = glm::length(forward);
        if (forwardLength <= epsilon)
            return glm::vec3(0.0f, 0.0f, -1.0f);

        return forward / forwardLength;
    }

    LightType getLightTypeFromLight(BaseLight* light)
    {
        if(auto directionalLight = dynamic_cast<DirectionalLight*>(light))
            return LightType::DIRECTIONAL;
        else if(auto pointLight = dynamic_cast<PointLight*>(light))
            return LightType::POINT;
        else if(auto spotLight = dynamic_cast<SpotLight*>(light))
            return LightType::SPOT;
        else
            return LightType::NONE;
    }

    LightType m_lightType{LightType::NONE};
    std::shared_ptr<BaseLight> m_light;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_LIGHT_COMPONENT_HPP
