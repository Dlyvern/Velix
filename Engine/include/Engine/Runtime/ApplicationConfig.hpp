#ifndef ELIX_APPLICATION_CONFIG_HPP
#define ELIX_APPLICATION_CONFIG_HPP

#include "Core/Macros.hpp"

#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ApplicationConfig
{
public:
    static ApplicationConfig fromArgs(int argc, char **argv);

    const std::vector<std::string> &getArgs() const;

    std::size_t getArgsSize() const;

private:
    std::vector<std::string> m_args;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_APPLICATION_CONFIG_HPP
