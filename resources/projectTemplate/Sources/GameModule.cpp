#ifndef ELIX_GAME_MODULE_HPP
#define ELIX_GAME_MODULE_HPP

#include "Engine/Scripting/ScriptsRegister.hpp"
#include "Engine/Scripting/ScriptMacroses.hpp"

extern "C" ELIX_API elix::engine::ScriptsRegister& getScriptsRegister()
{         
    return elix::engine::ScriptsRegister::instance();
}

#endif //ELIX_GAME_MODULE_HPP