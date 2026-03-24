#ifndef ELIX_IPLUGIN_HPP
#define ELIX_IPLUGIN_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IPlugin
{
public:
    virtual ~IPlugin() = default;

    virtual const char *getName() const = 0;
    virtual const char *getVersion() const = 0;

    virtual void onLoad() = 0;

    virtual void onUnload() = 0;
};

// Each plugin shared library must export these two C symbols:
//   extern "C" IPlugin* createPlugin();
//   extern "C" void     destroyPlugin(IPlugin*);
// Use the REGISTER_PLUGIN(ClassName) macro from VelixSDK/Plugin.hpp
// to generate them automatically.

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IPLUGIN_HPP
