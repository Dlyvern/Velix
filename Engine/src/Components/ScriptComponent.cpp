#include "Engine/Components/ScriptComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ScriptComponent::ScriptComponent(const Script& script) : m_script(script)
{

}

void ScriptComponent::update(float deltaTime)
{
    m_script.onUpdate(deltaTime);
}

ELIX_NESTED_NAMESPACE_END