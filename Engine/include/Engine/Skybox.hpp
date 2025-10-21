#ifndef ELIX_SKYBOX_HPP
#define ELIX_SKYBOX_HPP

#include "Core/Macros.hpp"
#include "Core/RenderPass.hpp"
#include "Core/GraphicsPipeline.hpp"
#include "Core/Buffer.hpp"
#include "Core/CommandPool.hpp"

#include "Engine/TextureImage.hpp"

#include <string>
#include <array>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Skybox
{
public:
    Skybox(VkDevice device, VkPhysicalDevice physicalDevice, core::CommandPool::SharedPtr commandPool, core::RenderPass::SharedPtr renderPass, const std::array<std::string, 6>& cubemaps);

private:
    core::Buffer::SharedPtr m_vertexBuffer{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SKYBOX_HPP