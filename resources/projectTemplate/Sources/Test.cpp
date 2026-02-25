#include "Engine/Scripting/ScriptMacroses.hpp"

class Test : public elix::engine::Script
{
public:
    void onUpdate(float deltaTime) override
    {
        VX_DEV_INFO_STREAM("OnUpdate");
    }

    void onStart() override
    {
        VX_DEV_INFO_STREAM("OnStart");
    }
};

REGISTER_SCRIPT(Test)
