#include <exception>
#include <iostream>

#include "Engine.hpp"

int main()
{
    try
    {
        Engine::run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "ENGINE_RUN_ERROR: EXCEPTION WHILE RUNNING THE ENGINE: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}