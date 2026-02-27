#ifndef ELIX_SCRIPTS_REGISTER_HPP
#define ELIX_SCRIPTS_REGISTER_HPP

#include "Engine/Scripting/Script.hpp"

#include <string>
#include <functional>
#include <unordered_map>
#include <optional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ScriptsRegister
{
public:
    //?Should we use pointer here
    using ScriptFactoryFunction = std::function<Script *()>;

    void registerScript(const std::string &name, const ScriptFactoryFunction &function);

    static ScriptsRegister &instance();

    const std::unordered_map<std::string, ScriptFactoryFunction> &getScripts() const;

    Script *createScript(const std::string name) const;

private:
    std::unordered_map<std::string, ScriptFactoryFunction> m_scripts;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCRIPTS_REGISTER_HPP