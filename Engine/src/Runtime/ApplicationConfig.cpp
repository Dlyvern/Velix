#include "Engine/Runtime/ApplicationConfig.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ApplicationConfig ApplicationConfig::fromArgs(int argc, char **argv)
{
    ApplicationConfig config;
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
