#ifndef ELIX_SCRIPT_MACROSES_HPP
#define ELIX_SCRIPT_MACROSES_HPP

#include "Engine/Scripting/ScriptsRegister.hpp"

#ifdef _WIN32
#define ELIX_API __declspec(dllexport)
#else
#define ELIX_API __attribute__((visibility("default")))
#endif

#define REGISTER_SCRIPT(Type) \
    namespace { \
        struct Type##Registrar { \
            Type##Registrar() { \
                elix::engine::ScriptsRegister::instance().registerScript(#Type, []() -> elix::engine::Script* { return new Type(); }); \
            } \
        }; \
        static Type##Registrar global_##Type##Registrar; \
    }

#endif //ELIX_SCRIPT_MACROSES_HPP