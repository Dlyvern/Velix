#include "Engine/PluginSystem/PluginLoader.hpp"

#include <iostream>
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

LibraryHandle PluginLoader::loadLibrary(const std::filesystem::path &libraryPath)
{
#ifdef _WIN32
    HMODULE lib = LoadLibraryW(libraryPath.c_str());

    if (!lib)
    {
        const DWORD err = GetLastError();

        LPWSTR msgBuf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       reinterpret_cast<LPWSTR>(&msgBuf), 0, nullptr);
        std::string reason = "(unknown error)";
        if (msgBuf)
        {

            const int len = WideCharToMultiByte(CP_UTF8, 0, msgBuf, -1, nullptr, 0, nullptr, nullptr);
            if (len > 0)
            {
                reason.resize(static_cast<size_t>(len - 1));
                WideCharToMultiByte(CP_UTF8, 0, msgBuf, -1, reason.data(), len, nullptr, nullptr);
            }
            LocalFree(msgBuf);
        }

        while (!reason.empty() && (reason.back() == '\n' || reason.back() == '\r' || reason.back() == ' '))
            reason.pop_back();

        VX_ENGINE_ERROR_STREAM("Failed to load library: " << libraryPath
                               << " — Win32 error " << err << ": " << reason << '\n');
    }

    return lib;
#else


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
