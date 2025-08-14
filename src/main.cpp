#include "Engine.hpp"

int main()
{
    try
    {
        Engine::run();
    }
    catch (const std::exception& e)
    {
        ELIX_LOG_ERROR("ENGINE_RUN_ERROR: EXCEPTION WHILE RUNNING THE ENGINE: ", e.what());
        return -1;
    }

    return 0;
}