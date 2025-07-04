#include "ElixirCore/ScriptsRegister.hpp"
#include "ElixirCore/ScriptMacros.hpp"

extern "C" ELIXIR_API const char** initScripts(int* count)
{
    const auto& names = ScriptsRegister::instance().getScriptNames();

    *count = static_cast<int>(names.size());

    static std::vector<const char*> cstrs;
    cstrs.clear();

    for (const auto& name : names)
        cstrs.push_back(name.c_str());

    return cstrs.data();
}

extern "C" ELIXIR_API ScriptsRegister* getScriptsRegister()
{
    return &ScriptsRegister::instance();
}