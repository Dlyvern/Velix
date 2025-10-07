#include "Core/ShaderHandler.hpp"

#include <stdexcept>
#include <fstream>
#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(core)

ShaderHandler::ShaderHandler() = default;

ShaderHandler::~ShaderHandler()
{
    if(m_shaderModule)
        vkDestroyShaderModule(VulkanContext::getContext()->getDevice(), m_shaderModule, nullptr);
}

//TODO get name from filename
void ShaderHandler::loadFromFile(const std::string& path, ShaderStage shaderStage)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
        throw std::runtime_error("Failed to open shader file: " + path);

    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    m_shaderModule = createShaderModule(buffer);

    switch (shaderStage)
    {
        case ShaderStage::VERTEX:   m_shaderStage = VK_SHADER_STAGE_VERTEX_BIT; break;
        case ShaderStage::FRAGMENT: m_shaderStage = VK_SHADER_STAGE_FRAGMENT_BIT; break;
        case ShaderStage::COMPUTE:  m_shaderStage = VK_SHADER_STAGE_COMPUTE_BIT; break;
    }

    createInfo();
}

//TODO get name from code
void ShaderHandler::loadFromCode(const std::vector<char>& code, ShaderStage shaderStage)
{
    m_shaderModule = createShaderModule(code);

    switch (shaderStage)
    {
        case ShaderStage::VERTEX:   m_shaderStage = VK_SHADER_STAGE_VERTEX_BIT; break;
        case ShaderStage::FRAGMENT: m_shaderStage = VK_SHADER_STAGE_FRAGMENT_BIT; break;
        case ShaderStage::COMPUTE:  m_shaderStage = VK_SHADER_STAGE_COMPUTE_BIT; break;
    }

    createInfo();
}

VkPipelineShaderStageCreateInfo ShaderHandler::getInfo()
{
    return m_info;
}

VkShaderModule ShaderHandler::getModule()
{
    return m_shaderModule;
}

VkShaderStageFlagBits ShaderHandler::getStage()
{
    return m_shaderStage;
}

const std::vector<char>& ShaderHandler::getCode() const
{
    return m_code;
}

void ShaderHandler::createInfo()
{
    m_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_info.stage = m_shaderStage;
    m_info.module = m_shaderModule;
    m_info.pName = "main";
}

VkShaderModule ShaderHandler::createShaderModule(const std::vector<char>& code)
{
    m_code = code;

    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;

    if(vkCreateShaderModule(VulkanContext::getContext()->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module");
    
    return shaderModule;
}

ELIX_NESTED_NAMESPACE_END