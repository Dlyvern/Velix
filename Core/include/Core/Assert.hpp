#ifndef ELIX_ASSERT_HPP
#define ELIX_ASSERT_HPP

#include "Core/Macros.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>

#if defined(_WIN32)
#define VX_DEBUG_BREAK() __debugbreak()
#else
#include <csignal>
#define VX_DEBUG_BREAK() raise(SIGTRAP)
#endif

#ifndef VX_ENABLE_ASSERTS
#ifdef DEBUG_BUILD
#define VX_ENABLE_ASSERTS 1
#else
#define VX_ENABLE_ASSERTS 0
#endif
#endif

#if defined(_MSC_VER)
#define VX_FUNC_SIG __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
#define VX_FUNC_SIG __PRETTY_FUNCTION__
#else
#define VX_FUNC_SIG __func__
#endif

#ifndef VX_ENABLE_FATAL_ASSERTS_IN_RELEASE
#define VX_ENABLE_FATAL_ASSERTS_IN_RELEASE 0
#endif

ELIX_NESTED_NAMESPACE_BEGIN(core)
enum class AssertAction
{
    Break,  // break into debugger / abort
    LogOnly // log and continue
};

inline void AssertReport(
    const char *expr,
    const char *file,
    int line,
    const char *func,
    const char *message,
    AssertAction action)
{
    std::fprintf(stderr, "\n================ ELIX ASSERT ================\n");
    std::fprintf(stderr, "Expression : %s\n", expr ? expr : "<null>");
    std::fprintf(stderr, "File       : %s\n", file ? file : "<null>");
    std::fprintf(stderr, "Line       : %d\n", line);
    std::fprintf(stderr, "Function   : %s\n", func ? func : "<null>");
    if (message && message[0] != '\0')
        std::fprintf(stderr, "Message    : %s\n", message);
    std::fprintf(stderr, "=============================================\n");
    std::fflush(stderr);

    if (action == AssertAction::Break)
    {
        VX_DEBUG_BREAK();
        std::abort();
    }
}

inline void AssertReportFmt(
    const char *expr,
    const char *file,
    int line,
    const char *func,
    AssertAction action,
    const char *fmt,
    ...)
{
    char buffer[2048]{};

    if (fmt)
    {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
    }

    AssertReport(expr, file, line, func, buffer, action);
}

[[noreturn]] inline void UnreachableImpl(const char *file, int line, const char *func, const char *message = "Reached unreachable code")
{
    AssertReport("VX_UNREACHABLE", file, line, func, message, AssertAction::Break);
    std::abort();
}

#if VX_ENABLE_ASSERTS

#define VX_ASSERT(expr)                                                                                                     \
    do                                                                                                                      \
    {                                                                                                                       \
        if (!(expr))                                                                                                        \
        {                                                                                                                   \
            ::elix::core::AssertReport(#expr, __FILE__, __LINE__, VX_FUNC_SIG, nullptr, ::elix::core::AssertAction::Break); \
        }                                                                                                                   \
    } while (0)

#define VX_ASSERT_MSG(expr, fmt, ...)                                                                                                     \
    do                                                                                                                                    \
    {                                                                                                                                     \
        if (!(expr))                                                                                                                      \
        {                                                                                                                                 \
            ::elix::core::AssertReportFmt(#expr, __FILE__, __LINE__, VX_FUNC_SIG, ::elix::core::AssertAction::Break, fmt, ##__VA_ARGS__); \
        }                                                                                                                                 \
    } while (0)

#elif VX_ENABLE_FATAL_ASSERTS_IN_RELEASE

// Optional mode: keep fatal asserts in release too
#define VX_ASSERT(expr)                                                                                                     \
    do                                                                                                                      \
    {                                                                                                                       \
        if (!(expr))                                                                                                        \
        {                                                                                                                   \
            ::elix::core::AssertReport(#expr, __FILE__, __LINE__, VX_FUNC_SIG, nullptr, ::elix::core::AssertAction::Break); \
        }                                                                                                                   \
    } while (0)

#define VX_ASSERT_MSG(expr, fmt, ...)                                                                                                     \
    do                                                                                                                                    \
    {                                                                                                                                     \
        if (!(expr))                                                                                                                      \
        {                                                                                                                                 \
            ::elix::core::AssertReportFmt(#expr, __FILE__, __LINE__, VX_FUNC_SIG, ::elix::core::AssertAction::Break, fmt, ##__VA_ARGS__); \
        }                                                                                                                                 \
    } while (0)

#else

#define VX_ASSERT(expr) ((void)0)
#define VX_ASSERT_MSG(expr, ...) ((void)0)

#endif

#if VX_ENABLE_ASSERTS

#define VX_VERIFY(expr) \
    ((expr) ? true : (::elix::core::AssertReport(#expr, __FILE__, __LINE__, VX_FUNC_SIG, nullptr, ::elix::core::AssertAction::Break), false))

#define VX_VERIFY_MSG(expr, fmt, ...) \
    ((expr) ? true : (::elix::core::AssertReportFmt(#expr, __FILE__, __LINE__, VX_FUNC_SIG, ::elix::core::AssertAction::Break, fmt, ##__VA_ARGS__), false))

#else

#define VX_VERIFY(expr) \
    ((expr) ? true : (::elix::core::AssertReport(#expr, __FILE__, __LINE__, VX_FUNC_SIG, nullptr, ::elix::core::AssertAction::LogOnly), false))

#define VX_VERIFY_MSG(expr, fmt, ...) \
    ((expr) ? true : (::elix::core::AssertReportFmt(#expr, __FILE__, __LINE__, VX_FUNC_SIG, ::elix::core::AssertAction::LogOnly, fmt, ##__VA_ARGS__), false))

#endif

#define VX_CHECK(expr) \
    ((expr) ? true : (::elix::core::AssertReport(#expr, __FILE__, __LINE__, VX_FUNC_SIG, nullptr, ::elix::core::AssertAction::LogOnly), false))

#define VX_CHECK_MSG(expr, fmt, ...) \
    ((expr) ? true : (::elix::core::AssertReportFmt(#expr, __FILE__, __LINE__, VX_FUNC_SIG, ::elix::core::AssertAction::LogOnly, fmt, ##__VA_ARGS__), false))

#define VX_ENSURE(expr) VX_CHECK(expr)
#define VX_ENSURE_MSG(expr, ...) VX_CHECK_MSG(expr, __VA_ARGS__)

#define VX_UNREACHABLE()                                                \
    do                                                                  \
    {                                                                   \
        ::elix::core::UnreachableImpl(__FILE__, __LINE__, VX_FUNC_SIG); \
    } while (0)

#define VX_UNREACHABLE_MSG(msg)                                                \
    do                                                                         \
    {                                                                          \
        ::elix::core::UnreachableImpl(__FILE__, __LINE__, VX_FUNC_SIG, (msg)); \
    } while (0)

#define VX_NOT_IMPLEMENTED() \
    VX_UNREACHABLE_MSG("Not implemented")

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSERT_HPP
