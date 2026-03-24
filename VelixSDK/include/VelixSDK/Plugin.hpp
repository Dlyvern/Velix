#ifndef ELIX_SDK_PLUGIN_HPP
#define ELIX_SDK_PLUGIN_HPP

#include "Engine/PluginSystem/IPlugin.hpp"

#if defined(_WIN32)
#define ELIX_PLUGIN_EXPORT __declspec(dllexport)
#else
#define ELIX_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

ELIX_NESTED_NAMESPACE_BEGIN(sdk)

class VXPlugin : public elix::engine::IPlugin
{
public:
    ~VXPlugin() override = default;
};

ELIX_NESTED_NAMESPACE_END

// Example:
//   class MyPlugin : public elix::sdk::VXPlugin { ... };
//   REGISTER_PLUGIN(MyPlugin)
#define REGISTER_PLUGIN(PluginClass)                                                                    \
    extern "C" ELIX_PLUGIN_EXPORT ::elix::engine::IPlugin *createPlugin() { return new PluginClass(); } \
    extern "C" ELIX_PLUGIN_EXPORT void destroyPlugin(::elix::engine::IPlugin *p) { delete p; }

#endif // ELIX_SDK_PLUGIN_HPP
