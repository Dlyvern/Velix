#ifndef SHADER_HPP
#define SHADER_HPP

#include "Core/ShaderHandler.hpp"
#include <vector>
#include <string>
#include <volk.h>
#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class Shader
{
public:
    using SharedPtr = std::shared_ptr<Shader>;

    Shader(const std::string& vertexPath, const std::string& fragmentPath);
    Shader(const std::vector<char>& vertexCode, const std::vector<char>& fragmentCode);

    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    ShaderHandler& getFragmentHandler();
    ShaderHandler& getVertexHandler();
    const std::vector<VkPipelineShaderStageCreateInfo>& getShaderStages() const;
private:
    ShaderHandler m_fragmentHandler;
    ShaderHandler m_vertexHandler;

    std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
};

ELIX_NESTED_NAMESPACE_END

#endif //SHADER_HPP