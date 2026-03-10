#include "Engine/Shaders/ShaderHotReloader.hpp"

#include "Engine/Shaders/ShaderCompiler.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(shaders)

ShaderHotReloader::ShaderHotReloader(std::filesystem::path shadersRootPath)
    : m_shadersRootPath(std::move(shadersRootPath))
{
}

void ShaderHotReloader::setShadersRootPath(const std::filesystem::path &path)
{
    m_shadersRootPath = path;
}

size_t ShaderHotReloader::recompileAll(std::vector<std::string> *outErrors)
{
    return ShaderCompiler::compileDirectoryToSpv(m_shadersRootPath, outErrors);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
