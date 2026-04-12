#include "Engine/Runtime/ApplicationConfig.hpp"

#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ApplicationConfig ApplicationConfig::fromArgs(int argc, char **argv)
{
    ApplicationConfig config;

#if defined(_WIN32)
    int wideArgCount = 0;
    LPWSTR *wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideArgCount);
    if (wideArgs)
    {
        config.m_args.reserve(static_cast<std::size_t>(wideArgCount));

        for (int i = 0; i < wideArgCount; ++i)
            config.m_args.emplace_back(std::filesystem::path(wideArgs[i] ? wideArgs[i] : L"").string());

        LocalFree(wideArgs);
        return config;
    }
#endif

    config.m_args.reserve(argc);

    for (int i = 0; i < argc; ++i)
        config.m_args.emplace_back(argv[i] ? argv[i] : "");

    return config;
}

const std::vector<std::string> &ApplicationConfig::getArgs() const
{
    return m_args;
}

std::size_t ApplicationConfig::getArgsSize() const
{
    return m_args.size();
}

ELIX_NESTED_NAMESPACE_END
