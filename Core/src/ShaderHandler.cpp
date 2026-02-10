#include "Core/ShaderHandler.hpp"

#include "Core/VulkanContext.hpp"

#include <stdexcept>
#include <fstream>
#include <iostream>

namespace
{
    std::vector<uint32_t> readSpirvFile(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            throw std::runtime_error("Failed to open SPIR-V file: " + path);

        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        if (size % 4 != 0)
            throw std::runtime_error("SPIR-V file size not divisible by 4: " + path);

        std::vector<uint32_t> spirv(size / sizeof(uint32_t));
        file.read(reinterpret_cast<char *>(spirv.data()), size);
        file.close();

        return spirv;
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(core)

ShaderHandler::ShaderHandler() = default;

ShaderHandler::~ShaderHandler()
{
    if (m_shaderModule)
        vkDestroyShaderModule(VulkanContext::getContext()->getDevice(), m_shaderModule, nullptr);
}

// TODO get name from filename
void ShaderHandler::loadFromFile(const std::string &path, ShaderStage shaderStage)
{
    auto buffer = readSpirvFile(path);

    m_shaderModule = createShaderModule(buffer);

    switch (shaderStage)
    {
    case ShaderStage::VERTEX:
        m_shaderStage = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case ShaderStage::FRAGMENT:
        m_shaderStage = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    case ShaderStage::COMPUTE:
        m_shaderStage = VK_SHADER_STAGE_COMPUTE_BIT;
        break;
    }

    createInfo();
}

// TODO get name from code
void ShaderHandler::loadFromCode(const std::vector<uint32_t> &code, ShaderStage shaderStage)
{
    m_shaderModule = createShaderModule(code);

    switch (shaderStage)
    {
    case ShaderStage::VERTEX:
        m_shaderStage = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case ShaderStage::FRAGMENT:
        m_shaderStage = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    case ShaderStage::COMPUTE:
        m_shaderStage = VK_SHADER_STAGE_COMPUTE_BIT;
        break;
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

const std::vector<uint32_t> &ShaderHandler::getCode() const
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

VkShaderModule ShaderHandler::createShaderModule(const std::vector<uint32_t> &code)
{
    m_code = code;

    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;

    if (vkCreateShaderModule(VulkanContext::getContext()->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module");

    return shaderModule;
}

ELIX_NESTED_NAMESPACE_END