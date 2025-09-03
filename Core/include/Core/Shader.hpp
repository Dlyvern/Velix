#ifndef SHADER_HPP
#define SHADER_HPP

#include "ShaderHandler.hpp"
#include <vector>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Shader
{
public:
    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    Shader(const std::vector<char>& vertexCode, const std::vector<char>& fragmentCode);

    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    ShaderHandler& getFragmentHandler();
    ShaderHandler& getVertexHandler();
private:
    ShaderHandler m_fragmentHandler;
    ShaderHandler m_vertexHandler;

    VkPipelineShaderStageCreateInfo m_shaderStages[];
};

ELIX_NESTED_NAMESPACE_END

#endif //SHADER_HPP