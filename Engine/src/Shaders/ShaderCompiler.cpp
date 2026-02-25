#include "Engine/Shaders/ShaderCompiler.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

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

bool ShaderCompiler::isCompilableShaderSource(const std::filesystem::path &sourcePath)
{
    return shaderKindFromPath(sourcePath).has_value();
}

std::optional<shaderc_shader_kind> ShaderCompiler::shaderKindFromPath(const std::filesystem::path &sourcePath)
{
    std::string extension = sourcePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });

    if (extension == ".vert")
        return shaderc_vertex_shader;
    if (extension == ".frag")
        return shaderc_fragment_shader;
    if (extension == ".comp")
        return shaderc_compute_shader;
    if (extension == ".geom")
        return shaderc_geometry_shader;
    if (extension == ".tesc")
        return shaderc_tess_control_shader;
    if (extension == ".tese")
        return shaderc_tess_evaluation_shader;

    return std::nullopt;
}

bool ShaderCompiler::compileFileToSpv(const std::filesystem::path &sourcePath,
                                      std::string *outError,
                                      const std::filesystem::path &outputPath)
{
    if (!std::filesystem::exists(sourcePath) || std::filesystem::is_directory(sourcePath))
    {
        if (outError)
            *outError = "Shader source file does not exist: " + sourcePath.string();

        return false;
    }

    const auto shaderKind = shaderKindFromPath(sourcePath);

    if (!shaderKind.has_value())
    {
        if (outError)
            *outError = "Unsupported shader extension: " + sourcePath.extension().string();

        return false;
    }

    std::ifstream sourceFile(sourcePath);

    if (!sourceFile.is_open())
    {
        if (outError)
            *outError = "Failed to open shader source: " + sourcePath.string();

        return false;
    }

    std::stringstream sourceStream;
    sourceStream << sourceFile.rdbuf();
    sourceFile.close();

    std::vector<uint32_t> spirv;
    try
    {
        spirv = compileGLSL(sourceStream.str(), shaderKind.value(), ShaderCompilerFlagBits::EDEFAULT, sourcePath.filename().string());
    }
    catch (const std::exception &error)
    {
        if (outError)
            *outError = "Shader compile error in " + sourcePath.string() + ":\n" + error.what();

        return false;
    }

    const std::filesystem::path resolvedOutputPath = outputPath.empty() ? std::filesystem::path(sourcePath.string() + ".spv") : outputPath;

    std::ofstream outputFile(resolvedOutputPath, std::ios::binary | std::ios::trunc);

    if (!outputFile.is_open())
    {
        if (outError)
            *outError = "Failed to open shader output file: " + resolvedOutputPath.string();

        return false;
    }

    outputFile.write(reinterpret_cast<const char *>(spirv.data()), static_cast<std::streamsize>(spirv.size() * sizeof(uint32_t)));
    outputFile.close();

    if (!outputFile.good())
    {
        if (outError)
            *outError = "Failed to write SPIR-V file: " + resolvedOutputPath.string();

        return false;
    }

    return true;
}

size_t ShaderCompiler::compileDirectoryToSpv(const std::filesystem::path &rootPath, std::vector<std::string> *outErrors)
{
    if (!std::filesystem::exists(rootPath) || !std::filesystem::is_directory(rootPath))
    {
        if (outErrors)
            outErrors->push_back("Shader directory does not exist: " + rootPath.string());

        return 0;
    }

    size_t compiledShadersCount = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(rootPath))
    {
        if (!entry.is_regular_file())
            continue;

        const std::filesystem::path sourcePath = entry.path();

        if (!isCompilableShaderSource(sourcePath))
            continue;

        std::string error;
        if (compileFileToSpv(sourcePath, &error))
            ++compiledShadersCount;
        else if (outErrors)
            outErrors->push_back(error);
    }

    return compiledShadersCount;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
