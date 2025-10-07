#ifndef ELIX_LIGHT_COMPONENT_HPP
#define ELIX_LIGHT_COMPONENT_HPP

#include "Engine/Lights.hpp"
#include "Engine/Components/ECS.hpp"

#include <memory>
#include <stdexcept>
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

        //Move parameters to a new light
        if(m_light)
        {
            newLight->color = m_light->color;
            newLight->position = m_light->position;

            m_light.reset();
        }

        m_light = newLight;
        m_lightType = getLightTypeFromLight(m_light.get());

        return m_light;
    }

    std::shared_ptr<BaseLight> getLight()
    {
        return m_light;
    }

private:
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