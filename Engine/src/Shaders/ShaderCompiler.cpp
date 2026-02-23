#include "Engine/Shaders/ShaderCompiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(shaders)

std::vector<uint32_t> ShaderCompiler::compileGLSL(const std::string &source, shaderc_shader_kind kind, uint8_t flags,
                                                  const std::string &name)
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);

    if (flags & ShaderCompilerFlagBits::ESHADER_COMPILER_FLAG_NO_WARNING)
        options.SetSuppressWarnings();
    else if (flags & ShaderCompilerFlagBits::ESHADER_COMPILER_FLAG_WARNINGS_AS_ERRORS)
        options.SetWarningsAsErrors();

    if (flags & ShaderCompilerFlagBits::ESHADER_COMPILER_FLAG_INVERT_Y)
        options.SetInvertY(true);

    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        source, kind, name.c_str(), options);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        throw std::runtime_error(result.GetErrorMessage());

    return {result.cbegin(), result.cend()};
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
