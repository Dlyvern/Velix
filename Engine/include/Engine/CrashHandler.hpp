#ifndef ELIX_CRASH_HANDLER_HPP
#define ELIX_CRASH_HANDLER_HPP

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <cxxabi.h>
#endif

#include <fstream>
#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// TODO Does not work for linux
class CrashHandler
{
public:
    static void setupCrashHandler();

private:
#ifdef _WIN32
    static LONG WINAPI crashHandler(EXCEPTION_POINTERS *ExceptionInfo);
#else
    static void crashHandler(int signum);
#endif
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_CRASH_HANDLER_HPP