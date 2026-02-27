#include "Core/Logger.hpp"
#include "Editor/Runtime/EditorRuntime.hpp"
#include "Engine/Runtime/ApplicationLoop.hpp"

int main(int argc, char **argv)
{
    try
    {
        auto config = elix::engine::ApplicationConfig::fromArgs(argc, argv);
        elix::engine::ApplicationLoop loop{};
        return loop.run(config, [](const elix::engine::ApplicationConfig &applicationConfig)
                        { return std::make_unique<elix::editor::EditorRuntime>(applicationConfig); });
    }
    catch (const std::exception &e)
    {
        VX_DEV_ERROR_STREAM("Fatal error: " << e.what());
        return 1;
    }
}