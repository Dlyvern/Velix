#ifndef GAME_SCRIPT_HPP
#define GAME_SCRIPT_HPP

#include "VelixFlow/Script.hpp"

class GameScript final : public elix::scripting::Script
{
public:
    void onStart() override;

    void onUpdate(float deltaTime) override;

    std::string getScriptName() const override;
};

#endif //GAME_SCRIPT_HPP
