#include "Engine/PluginSystem/PluginLoader.hpp"

#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

LibraryHandle PluginLoader::loadLibrary(const std::string& libraryPath)
{
#ifdef _WIN32
    HMODULE lib = LoadLibrary(libraryPath.c_str());

    if (!lib)
        std::cerr << "Failed to load library " << std::endl;

    return lib;
#else
    void* handle = dlopen(libraryPath.c_str(), RTLD_LAZY);

    if (!handle)
        std::cerr << "Failed to load library " << dlerror();

    return handle;
#endif
}

void* PluginLoader::getFunction(const std::string& functionName, LibraryHandle library)
{
#ifdef _WIN32
    FARPROC function = GetProcAddress(library, functionName.c_str());

    if (!function)
        std::cerr << "Failed to get function" << std::endl;

    return reinterpret_cast<void*>(function);
#else
    void* function = dlsym(library, functionName.c_str());

    if (!function)
        std::cerr << "Failed to get function " << dlerror() << std::endl;

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