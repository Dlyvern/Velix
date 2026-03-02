#include "Engine/Scripting/ScriptsRegister.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    ScriptsRegister *g_activeScriptsRegister{nullptr};
}

void ScriptsRegister::registerScript(const std::string &name, const ScriptFactoryFunction &function)
{
    m_scripts[name] = function;
}

ScriptsRegister &ScriptsRegister::instance()
{
    static ScriptsRegister instance;
    return instance;
}

void ScriptsRegister::setActiveRegister(ScriptsRegister *registerInstance)
{
    g_activeScriptsRegister = registerInstance;
}

ScriptsRegister *ScriptsRegister::getActiveRegister()
{
    return g_activeScriptsRegister;
}

Script *ScriptsRegister::createScriptFromActiveRegister(const std::string &name)
{
    if (g_activeScriptsRegister)
    {
        if (auto *script = g_activeScriptsRegister->createScript(name))
            return script;
    }

    return ScriptsRegister::instance().createScript(name);
}

const std::unordered_map<std::string, ScriptsRegister::ScriptFactoryFunction> &ScriptsRegister::getScripts() const
{
    return m_scripts;
}

Script *ScriptsRegister::createScript(const std::string &name) const
{
    auto it = m_scripts.find(name);

    if (it == m_scripts.end())
        return nullptr;

    return it->second();
}

ELIX_NESTED_NAMESPACE_END
