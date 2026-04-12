#include "Engine/PluginSystem/PluginLoader.hpp"

#include <iostream>
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

LibraryHandle PluginLoader::loadLibrary(const std::filesystem::path &libraryPath)
{
#ifdef _WIN32
    HMODULE lib = LoadLibraryW(libraryPath.c_str());

    if (!lib)
        VX_ENGINE_ERROR_STREAM("Failed to load library: " << libraryPath << '\n');

    return lib;
#else
    // RTLD_GLOBAL allows dependent symbols to be resolved across loaded modules.
    // RTLD_NOW fails early if something is missing.
    void *handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_GLOBAL);

    if (!handle)
        VX_ENGINE_ERROR_STREAM("Failed to load library " << dlerror());

    return handle;
#endif
}

LibraryHandle PluginLoader::loadLibrary(const std::string &libraryPath)
{
    return loadLibrary(std::filesystem::path(libraryPath));
}

void *PluginLoader::getFunction(const std::string &functionName, LibraryHandle library)
{
#ifdef _WIN32
    FARPROC function = GetProcAddress(library, functionName.c_str());
    return reinterpret_cast<void *>(function);
#else
    // Clear any prior error, then look up the symbol.
    dlerror();
    void *function = dlsym(library, functionName.c_str());
    return function;
#endif
}

void PluginLoader::closeLibrary(LibraryHandle library)
{
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
}

ELIX_NESTED_NAMESPACE_END
