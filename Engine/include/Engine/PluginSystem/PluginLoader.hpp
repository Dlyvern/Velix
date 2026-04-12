#ifndef ELIX_PLUGIN_LOADER_HPP
#define ELIX_PLUGIN_LOADER_HPP

#include "Core/Macros.hpp"

#include <filesystem>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #ifdef min
        #undef min
    #endif
    #ifdef max
        #undef max
    #endif
    using LibraryHandle = HMODULE;
#else
    #include <dlfcn.h>
    using LibraryHandle = void*;
#endif

namespace PluginLoader
{
    LibraryHandle loadLibrary(const std::filesystem::path& libraryPath);
    LibraryHandle loadLibrary(const std::string& libraryPath);
    void* getFunction(const std::string& functionName, LibraryHandle library);
    void closeLibrary(LibraryHandle library);

    template<typename T>
    T getFunction(const std::string& functionName, LibraryHandle library)
    {
        void* rawFunc = getFunction(functionName, library);
        return reinterpret_cast<T>(rawFunc);
    }
} //namespace PluginLoader

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_PLUGIN_LOADER_HPP
