#ifndef ELIX_SHADER_HOT_RELOADER_HPP
#define ELIX_SHADER_HOT_RELOADER_HPP

#include "Core/Macros.hpp"

#include <filesystem>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(shaders)

class ShaderHotReloader
{
public:
    explicit ShaderHotReloader(std::filesystem::path shadersRootPath = "./resources/shaders");

    void setShadersRootPath(const std::filesystem::path &path);

    size_t recompileAll(std::vector<std::string> *outErrors = nullptr);

private:
    std::filesystem::path m_shadersRootPath;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADER_HOT_RELOADER_HPP
