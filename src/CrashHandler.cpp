#include "CrashHandler.hpp"

#ifdef _WIN32
    #include <windows.h>
    #include <dbghelp.h>
#endif

#ifndef _WIN32
    #include <execinfo.h>
    #include <dlfcn.h>
#endif

#include <stdexcept>

#include <csignal>
#include <cstdlib>

#include "VelixFlow/Logger.hpp"

#include <unistd.h>
#include <sstream>
#include <cstdlib>
#include <string>
#include <cstring>


#ifndef _WIN32
    void printBacktraceWithAddr2Line()
    {
        void* callstack[128];
        int frames = backtrace(callstack, 128);

        char exePath[1024] = {};
        readlink("/proc/self/exe", exePath, sizeof(exePath));

        for (int i = 0; i < frames; ++i)
        {
            std::stringstream cmd;
            cmd << "addr2line -e " << exePath << " -f -C " << callstack[i];
            FILE* fp = popen(cmd.str().c_str(), "r");
            if (fp)
            {
                char function[512];
                char location[512];
                if (fgets(function, sizeof(function), fp) &&
                    fgets(location, sizeof(location), fp))
                {
                    function[strcspn(function, "\n")] = 0;
                    location[strcspn(location, "\n")] = 0;
                    printf("   %s at %s\n", function, location);
                }
                pclose(fp);
            }
        }
    }
#endif

void signalHandler(int signal)
{
#if defined(_WIN32)
    void* stack[50];
    unsigned short frames;
    SYMBOL_INFO* symbol;
    HANDLE process = GetCurrentProcess();

    SymInitialize(process, NULL, TRUE);
    frames = CaptureStackBackTrace(0, 50, stack, NULL);
    symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    ELIX_LOG_ERROR("Signal ", signal);

    for (unsigned int i = 0; i < frames; i++) 
    {
        SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);

        // ELIX_LOG_ERROR(frames - i - 1, symbol->Name, symbol->Address)
        // ELIX_LOG_ERROR("%i: %s - 0x%0X", frames - i - 1, symbol->Name, symbol->Address);
    }

    free(symbol);
#elif defined(__linux__) || defined(__APPLE__)
    void* array[25];
    size_t size = backtrace(array, 25);
    char** strings = backtrace_symbols(array, size);

    ELIX_LOG_ERROR("Signal ", signal);

    for (size_t i = 0; i < size; i++)
        ELIX_LOG_ERROR(strings[i]);

    free(strings);
#else
    //It should not happened.... Maybe...
    ELIX_LOG_ERROR("Signal ", signal, " received. (No backtrace on this platform)");
#endif

#ifndef _WIN32
    printBacktraceWithAddr2Line();
#endif

    std::exit(signal);
}

void terminateHandler()
{
    ELIX_LOG_ERROR("Unhandled exception. Terminating...");

    std::abort();
}

void CrashHandler::init()
{
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE,  signalHandler);
    std::signal(SIGTERM, signalHandler);

#ifndef _WIN32
    std::signal(SIGKILL, signalHandler);
#endif

    std::set_terminate(terminateHandler);

}