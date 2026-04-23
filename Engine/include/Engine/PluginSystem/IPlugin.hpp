#ifndef ELIX_IPLUGIN_HPP
#define ELIX_IPLUGIN_HPP

#include "Core/Macros.hpp"

#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IPlugin
{
public:
    virtual ~IPlugin() = default;

    virtual const char *getName() const = 0;
    virtual const char *getVersion() const = 0;

    virtual void onLoad() = 0;

    virtual void onUnload() = 0;



    virtual std::vector<std::string> getDependencies() const { return {}; }
};







ELIX_NESTED_NAMESPACE_END

#endif
