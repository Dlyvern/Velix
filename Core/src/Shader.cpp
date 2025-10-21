#include "Core/Shader.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)

Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath)
{
    m_vertexHandler.loadFromFile(vertexPath, ShaderStage::VERTEX);
    m_fragmentHandler.loadFromFile(fragmentPath, ShaderStage::FRAGMENT);
    m_shaderStages = {m_vertexHandler.getInfo(), m_fragmentHandler.getInfo()};
}

Shader::Shader(const std::vector<uint32_t>& vertexCode, const std::vector<uint32_t>& fragmentCode)
{
    m_vertexHandler.loadFromCode(vertexCode, ShaderStage::VERTEX);
    m_fragmentHandler.loadFromCode(fragmentCode, ShaderStage::FRAGMENT);
    m_shaderStages = {m_vertexHandler.getInfo(), m_fragmentHandler.getInfo()};
}

const std::vector<VkPipelineShaderStageCreateInfo>& Shader::getShaderStages() const
{
    return m_shaderStages;
}

ShaderHandler& Shader::getFragmentHandler()
{
    return m_fragmentHandler;
}

ShaderHandler& Shader::getVertexHandler()
{
    return m_vertexHandler;
}

Shader::~Shader() = default;

ELIX_NESTED_NAMESPACE_END