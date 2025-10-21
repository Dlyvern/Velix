#ifndef SHADER_HANDLER_HPP
#define SHADER_HANDLER_HPP

#include "Macros.hpp"
#include "Core/VulkanContext.hpp"
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(core)

enum class ShaderStage
{
    VERTEX,
    FRAGMENT,
    COMPUTE
};

class ShaderHandler
{
public:
    void loadFromFile(const std::string& path, ShaderStage shaderStage);
    void loadFromCode(const std::vector<uint32_t>& code, ShaderStage shaderStage);

    VkShaderModule getModule();
    VkShaderStageFlagBits getStage();
    VkPipelineShaderStageCreateInfo getInfo();

    const std::vector<uint32_t>& getCode() const;

    ShaderHandler(const ShaderHandler&) = delete;
    ShaderHandler& operator=(const ShaderHandler&) = delete;
    ShaderHandler();
    ~ShaderHandler();
private:
    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);
    void createInfo();
    
    VkShaderModule m_shaderModule{VK_NULL_HANDLE};
    VkShaderStageFlagBits m_shaderStage;

    VkPipelineShaderStageCreateInfo m_info{};

    std::vector<uint32_t> m_code;
};

ELIX_NESTED_NAMESPACE_END

#endif //SHADER_HANDLER_HPP