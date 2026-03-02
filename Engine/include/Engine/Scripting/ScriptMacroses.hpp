#ifndef ELIX_SCRIPT_MACROSES_HPP
#define ELIX_SCRIPT_MACROSES_HPP

#include "Engine/Scripting/ScriptsRegister.hpp"

#ifdef _WIN32
#define ELIX_API __declspec(dllexport)
#else
#define ELIX_API __attribute__((visibility("default")))
#endif

#define REGISTER_SCRIPT(Type)                                                                                                           \
    namespace                                                                                                                           \
    {                                                                                                                                   \
        struct Type##Registrar                                                                                                          \
        {                                                                                                                               \
            Type##Registrar()                                                                                                           \
            {                                                                                                                           \
                elix::engine::ScriptsRegister::instance().registerScript(#Type, []() -> elix::engine::Script * { return new Type(); }); \
            }                                                                                                                           \
        };                                                                                                                              \
        static Type##Registrar global_##Type##Registrar;                                                                                \
    }

#define VX_VARIABLE(Type, Name) ::elix::engine::Script::Variable<Type> Name{#Name}
#define VX_VARIABLE_DEFAULT(Type, Name, DefaultValue) ::elix::engine::Script::Variable<Type> Name{#Name, (DefaultValue)}
#define VX_ENTITY_VARIABLE(Name) ::elix::engine::Script::Variable<::elix::engine::Script::EntityRef> Name{#Name}
#define VX_ENTITY_VARIABLE_DEFAULT(Name, EntityId) ::elix::engine::Script::Variable<::elix::engine::Script::EntityRef> Name{#Name, ::elix::engine::Script::EntityRef(static_cast<uint32_t>(EntityId))}

#endif // ELIX_SCRIPT_MACROSES_HPP
