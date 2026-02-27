#ifndef ELIX_API_SDK_HPP
#define ELIX_API_SDK_HPP

#if defined(_WIN32)
#if defined(VELIX_SDK_BUILD)
#define VELIX_SDK_API __declspec(dllexport)
#else
#define VELIX_SDK_API __declspec(dllimport)
#endif
#else
#if defined(VELIX_SDK_BUILD)
#define VELIX_SDK_API __attribute__((visibility("default")))
#else
#define VELIX_SDK_API
#endif
#endif

#endif // ELIX_API_SDK_HPP