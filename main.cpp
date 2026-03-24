#include "Engine/Diagnostics.hpp"
#include "Core/Logger.hpp"
#include "Editor/Runtime/EditorRuntime.hpp"
#include "Engine/Runtime/ApplicationLoop.hpp"
#include "Engine/Runtime/GameRuntime.hpp"

#include <exception>
#include <memory>

int main(int argc, char **argv)
{
    const auto logFilePath = elix::engine::diagnostics::configureDefaultLogging();
    elix::engine::diagnostics::installCrashHandler();

    if (!logFilePath.empty())
        VX_CORE_INFO_STREAM("Log file: " << logFilePath.string() << '\n');

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
        const auto crashFilePath = elix::engine::diagnostics::writeCrashReport("Fatal exception", e.what());
        VX_DEV_ERROR_STREAM("Fatal error: " << e.what());
        if (!crashFilePath.empty())
            VX_DEV_ERROR_STREAM("Crash report written to: " << crashFilePath.string() << '\n');
        return 1;
    }
    catch (...)
    {
        const auto crashFilePath = elix::engine::diagnostics::writeCrashReport("Fatal exception", "Unknown non-std exception.");
        VX_DEV_ERROR_STREAM("Fatal error: unknown non-std exception\n");
        if (!crashFilePath.empty())
            VX_DEV_ERROR_STREAM("Crash report written to: " << crashFilePath.string() << '\n');
        return 1;
    }
}
