#include "Engine/Components/ScriptComponent.hpp"
#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ScriptComponent::ScriptComponent(Script* script) : m_script(script)
{

    
}

void ScriptComponent::onAttach()
{
    ECS::onAttach();
    m_script->onStart();
}

void ScriptComponent::onDetach()
{
    ECS::onDetach();
    delete m_script;
    m_script = nullptr;
}

void ScriptComponent::update(float deltaTime)
{
    ECS::update(deltaTime);
    
    if(!m_script)
    {
        VX_ENGINE_ERROR_STREAM("Script is not valid. Something is weird" << std::endl);
        return;
    }

    m_script->onUpdate(deltaTime);
}

ELIX_NESTED_NAMESPACE_END