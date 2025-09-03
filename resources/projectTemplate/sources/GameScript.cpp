#include "GameScript.hpp"
#include "VelixFlow/ScriptsRegister.hpp"
#include <iostream>
#include "VelixFlow/ScriptMacros.hpp"

void GameScript::onStart()
{
    std::cout << "GameScript::onStart()" << std::endl;
}

void GameScript::onUpdate(float deltaTime)
{
    std::cout << "GameScript::onUpdate()" << std::endl;
}

std::string GameScript::getScriptName() const
{
    return "GameScript";
}

REGISTER_SCRIPT(GameScript)