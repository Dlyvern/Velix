#include <iostream>
#include "Engine/Scripting/ScriptMacroses.hpp"

class Test : public elix::engine::Script
{
public:
    void onUpdate(float deltaTime) override
    {
        std::cout << "OnUpdate" << std::endl;
    }

    void onStart() override
    {
        std::cout << "OnStart" << std::endl;
    }
};

REGISTER_SCRIPT(Test)