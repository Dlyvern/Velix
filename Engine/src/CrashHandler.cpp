#include "Engine/CrashHandler.hpp"

#ifdef _WIN32
    #include <windows.h>
    #include <dbghelp.h>
    #include <process.h>
#else
    #include <execinfo.h>
    #include <dlfcn.h>
    #include <unistd.h>
#endif

#include <stdexcept>
#include <csignal>
#include <cstdlib>
#include <sstream>
#include <string>
#include <cstring>
#include <iostream>

#ifndef _WIN32
void printBacktraceWithAddr2Line()
{
    void* callstack[128];
    int frames = backtrace(callstack, 128);

    char exePath[1024] = {};
    auto something = readlink("/proc/self/exe", exePath, sizeof(exePath));

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

    std::cout << "Signal: " << signal << '\n';

    for (unsigned int i = 0; i < frames; i++) 
    {
        SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
        std::cout << frames - i - 1 << ": " << symbol->Name << " - 0x" << std::hex << symbol->Address << std::dec << '\n';
    }

    free(symbol);
#else
    void* array[25];
    size_t size = backtrace(array, 25);
    char** strings = backtrace_symbols(array, size);

    std::cout << "Signal: " << signal << '\n';

    for (size_t i = 0; i < size; i++)
        std::cout << strings[i] << '\n';

    free(strings);
#endif

#ifndef _WIN32
    printBacktraceWithAddr2Line();
#endif

    std::exit(signal);
}

void terminateHandler()
{
    std::cout << "Unhandled exception. Terminating..." << '\n';
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