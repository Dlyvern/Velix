#include "GameScript.hpp"
#include "ElixirCore/ScriptsRegister.hpp"
#include <iostream>
#include "ElixirCore/ScriptMacros.hpp"

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