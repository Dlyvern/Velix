#include "Engine/Caches/ShaderCache.hpp"
#include "Engine/Hash.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

size_t ShaderCache::createOrGetShaderCache(const std::string& vertexPath, const std::string& fragmentPath)
{
    size_t hashData{0};

    auto shader = std::make_shared<core::Shader>(vertexPath,  fragmentPath);

    for(const auto& fragmentCode : shader->getFragmentHandler().getCode())
        hashing::hash(hashData, fragmentCode);
    
    for(const auto& vertexCode : shader->getVertexHandler().getCode())
        hashing::hash(hashData, vertexCode);
    
    if(auto it = m_shadersCache.find(hashData); it == m_shadersCache.end())
        m_shadersCache[hashData] = std::move(shader);

    return hashData;
}

core::Shader::SharedPtr ShaderCache::getShader(size_t cache) const
{
    auto it = m_shadersCache.find(cache);
    return it != m_shadersCache.end() ? it->second : nullptr;
}

ELIX_NESTED_NAMESPACE_END