#include "Engine/CrashHandler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void CrashHandler::setupCrashHandler()
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(CrashHandler::crashHandler);
#else
    signal(SIGSEGV, CrashHandler::crashHandler);
    signal(SIGABRT, CrashHandler::crashHandler);
#endif
}

#ifdef _WIN32
LONG WINAPI CrashHandler::crashHandler(EXCEPTION_POINTERS *ExceptionInfo)
{
    std::ofstream file("crash_log.txt");
    file << "Crashed. Exception code: " << std::hex << ExceptionInfo->ExceptionRecord->ExceptionCode << "\n\n";

    void *stack[64];
    USHORT frames = CaptureStackBackTrace(0, 64, stack, nullptr);

    file << "Stack trace:\n";

    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);

    for (USHORT i = 0; i < frames; i++)
    {
        DWORD64 addr = (DWORD64)(stack[i]);

        char buffer[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;

        IMAGEHLP_LINE64 line;
        DWORD displacement;

        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

        if (SymGetLineFromAddr64(process, addr, &displacement, &line))
        {
            file << "    at " << line.FileName << ":" << line.LineNumber << "\n";
        }

        if (SymFromAddr(process, addr, 0, symbol))
            file << i << ": " << symbol->Name << " - 0x" << std::hex << symbol->Address << "\n";
        else
            file << i << ": [0x" << std::hex << addr << "]\n";
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#else

static std::string demangle(const char *name)
{
    int status = 0;
    char *demangled = abi::__cxa_demangle(name, nullptr, nullptr, &status);
    std::string result = (status == 0 && demangled) ? demangled : name;
    free(demangled);
    return result;
}

static std::string resolveAddr(void *addr)
{
    std::ostringstream cmd;
    // Cast pointer to uintptr_t and print in hex
    cmd << "addr2line -f -C -e /proc/self/exe 0x"
        << std::hex << reinterpret_cast<uintptr_t>(addr);

    FILE *pipe = popen(cmd.str().c_str(), "r");
    if (!pipe)
        return "??\n";

    char buffer[512];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr)
        result += buffer;

    pclose(pipe);
    return result;
}

void CrashHandler::crashHandler(int signum)
{
    std::ofstream file("crash_log.txt");
    file << "Crashed. Signal: " << signum << "\n\n";

    void *array[64];
    size_t size = backtrace(array, 64);

    file << "Stack trace:\n";

    for (size_t i = 0; i < size; i++)
    {
        file << "[" << i << "] " << resolveAddr(array[i]);
    }

    file.close();
    _Exit(1);
}
#endif

ELIX_NESTED_NAMESPACE_END