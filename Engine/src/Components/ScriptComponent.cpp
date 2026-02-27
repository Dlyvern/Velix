#include "Engine/Components/ScriptComponent.hpp"
#include "Engine/Entity.hpp"
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ScriptComponent::ScriptComponent(Script *script) : ScriptComponent("", script)
{
}

ScriptComponent::ScriptComponent(const std::string &scriptName, Script *script) : m_script(script),
                                                                                  m_scriptName(scriptName)
{
}

void ScriptComponent::onAttach()
{
    ECS::onAttach();

    if (!m_script)
    {
        VX_ENGINE_ERROR_STREAM("ScriptComponent::onAttach: script pointer is null\n");
        return;
    }

    m_script->setOwnerEntity(getOwner<Entity>());
    m_isAttached = true;
    m_script->onStart();
}

void ScriptComponent::onDetach()
{
    ECS::onDetach();

    if (!m_isAttached)
        return;

    if (m_script)
    {
        m_script->onStop();
        m_script->setOwnerEntity(nullptr);
    }

    m_isAttached = false;
}

void ScriptComponent::update(float deltaTime)
{
    ECS::update(deltaTime);

    if (!m_isAttached)
        return;

    if (!m_script)
    {
        VX_ENGINE_ERROR_STREAM("ScriptComponent::update: script pointer is null\n");
        return;
    }

    m_script->onUpdate(deltaTime);
}

const std::string &ScriptComponent::getScriptName() const
{
    return m_scriptName;
}

Script *ScriptComponent::getScript() const
{
    return m_script;
}

void ScriptComponent::onOwnerAttached()
{
    ECS::onOwnerAttached();

    if (m_script)
        m_script->setOwnerEntity(getOwner<Entity>());
}

ScriptComponent::~ScriptComponent()
{
    delete m_script;
    m_script = nullptr;
}

ELIX_NESTED_NAMESPACE_END
