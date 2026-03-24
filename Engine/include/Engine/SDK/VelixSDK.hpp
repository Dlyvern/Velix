#ifndef ELIX_VELIX_SDK_HPP
#define ELIX_VELIX_SDK_HPP

// Core SDK classes
#include "Engine/SDK/VXGameState.hpp"
#include "Engine/SDK/VXObject.hpp"
#include "Engine/SDK/VXActor.hpp"
#include "Engine/SDK/VXCharacter.hpp"

// Exposed variable macros: VX_VARIABLE, VX_VARIABLE_DEFAULT, VX_ENTITY_VARIABLE,
//                          VX_SCRIPT_VARIABLE, GET_SCRIPT, REGISTER_SCRIPT
#include "Engine/Scripting/ScriptMacroses.hpp"

// Input polling: InputManager::instance().isKeyDown(KeyCode::W) etc.
#include "Engine/Input/InputManager.hpp"
#include "Engine/Input/Keyboard.hpp"

// Time: Time::deltaTime(), Time::totalTime(), Time::setTimeScale()
#include "Engine/Time.hpp"

// Physics raycast
#include "Engine/Scripting/VelixAPI.hpp"

#endif // ELIX_VELIX_SDK_HPP
