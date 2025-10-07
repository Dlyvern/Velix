#ifndef ELIX_SHADER_CACHE_HPP
#define ELIX_SHADER_CACHE_HPP

#include "Core/Macros.hpp"
#include "Core/Shader.hpp"

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ShaderCache
{
public:
    size_t createOrGetShaderCache(const std::string& vertexPath, const std::string& fragmentPath);
    core::Shader::SharedPtr getShader(size_t cache) const;
private:
    std::unordered_map<size_t, core::Shader::SharedPtr> m_shadersCache;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SHADER_CACHE_HPP