#include "Core/ShaderHandler.hpp"

#include "Core/VulkanContext.hpp"
#include "Core/VulkanAssert.hpp"

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

namespace
{
    VkShaderStageFlagBits toVkShaderStage(ShaderStage shaderStage)
    {
        switch (shaderStage)
        {
        case ShaderStage::VERTEX:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::FRAGMENT:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::COMPUTE:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        case ShaderStage::RAYGEN:
            return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        case ShaderStage::MISS:
            return VK_SHADER_STAGE_MISS_BIT_KHR;
        case ShaderStage::CLOSEST_HIT:
            return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        }

        throw std::runtime_error("Unsupported shader stage");
    }
}

void ShaderHandler::destroyVk()
{
    if (m_shaderModule)
    {
        vkDestroyShaderModule(VulkanContext::getContext()->getDevice(), m_shaderModule, nullptr);
        m_shaderModule = VK_NULL_HANDLE;
    }
}

ShaderHandler::~ShaderHandler()
{
    destroyVk();
}

// TODO get name from filename
void ShaderHandler::loadFromFile(const std::string &path, ShaderStage shaderStage)
{
    auto buffer = readSpirvFile(path);

    m_shaderModule = createShaderModule(buffer);
    m_shaderStage = toVkShaderStage(shaderStage);

    createInfo();
}

// TODO get name from code
void ShaderHandler::loadFromCode(const std::vector<uint32_t> &code, ShaderStage shaderStage)
{
    m_shaderModule = createShaderModule(code);
    m_shaderStage = toVkShaderStage(shaderStage);

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

    VX_VK_CHECK(vkCreateShaderModule(VulkanContext::getContext()->getDevice(), &createInfo, nullptr, &shaderModule));

    return shaderModule;
}

ELIX_NESTED_NAMESPACE_END
