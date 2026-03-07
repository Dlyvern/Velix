#include "Core/Logger.hpp"
#include "Editor/Runtime/EditorRuntime.hpp"
#include "Engine/Runtime/ApplicationLoop.hpp"
#include "Engine/Runtime/GameRuntime.hpp"

int main(int argc, char **argv)
{
    try
    {
        auto config = elix::engine::ApplicationConfig::fromArgs(argc, argv);
        elix::engine::ApplicationLoop loop{};

        const bool useGameRuntime = elix::engine::GameRuntime::shouldRunForConfig(config);
        return loop.run(config, [useGameRuntime](const elix::engine::ApplicationConfig &applicationConfig) -> std::unique_ptr<elix::engine::IRuntime>
                        {
                            if (useGameRuntime)
                                return std::make_unique<elix::engine::GameRuntime>(applicationConfig);

                            return std::make_unique<elix::editor::EditorRuntime>(applicationConfig); });
    }
    catch (const std::exception &e)
    {
        VX_DEV_ERROR_STREAM("Fatal error: " << e.what());
        return 1;
    }
}
