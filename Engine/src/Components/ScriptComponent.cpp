#include "Engine/Components/ScriptComponent.hpp"
#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ScriptComponent::ScriptComponent(Script* script) : m_script(script)
{

    
}

void ScriptComponent::destroy()
{
    delete m_script;
    m_script = nullptr;
}

void ScriptComponent::update(float deltaTime)
{
    if(!m_script)
    {
        std::cerr << "Script is not valid. Something is weird" << std::endl;
        return;
    }

    m_script->onUpdate(deltaTime);
}

ELIX_NESTED_NAMESPACE_END