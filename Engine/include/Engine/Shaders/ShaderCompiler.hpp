#ifndef ELIX_SHADER_COMPILER_HPP
#define ELIX_SHADER_COMPILER_HPP

#include "Core/Macros.hpp"

#include <shaderc/shaderc.hpp>

#include <vector>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(shaders)

class ShaderCompiler
{
public:
    enum ShaderCompilerFlagBits : uint8_t
    {
        EDEFAULT = 0,
        ESHADER_COMPILER_FLAG_NO_WARNING = 1 << 0,
        ESHADER_COMPILER_FLAG_WARNINGS_AS_ERRORS = 1 << 1,
        ESHADER_COMPILER_FLAG_INVERT_Y = 1 << 2
    };

    static std::vector<uint32_t> compileGLSL(const std::string &source,
                                             shaderc_shader_kind kind, uint8_t flags = 0,
                                             const std::string &name = "shader");
};

ELIX_CUSTOM_NAMESPACE_END ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADER_COMPILER_HPP