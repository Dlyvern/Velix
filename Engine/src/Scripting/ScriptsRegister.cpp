#include "Engine/Scripting/ScriptsRegister.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void ScriptsRegister::registerScript(const std::string& name, const ScriptFactoryFunction& function)
{
    m_scripts[name] = function;
}

ScriptsRegister& ScriptsRegister::instance()
{
    static ScriptsRegister instance;
    return instance;
}

const std::unordered_map<std::string, ScriptsRegister::ScriptFactoryFunction>& ScriptsRegister::getScripts() const
{
    return m_scripts;
}

Script* ScriptsRegister::createScript(const std::string name) const
{
    auto it = m_scripts.find(name);

    if(it == m_scripts.end())
        return nullptr;

    auto script = it->second();

    return script;
}

ELIX_NESTED_NAMESPACE_END