#include <iostream>
#include "Core/Window.hpp"
#include "Core/VulkanContext.hpp"
#include "Core/Render.hpp"
#include "Core/Shader.hpp"

int main(int argc, char** argv)
{
    auto window = elix::platform::Window::create(1280, 720, "Window");
    auto vulkanContext = elix::core::VulkanContext::create(window);

    elix::core::Shader shader("/home/dlyvern/Projects/Velix/resources/shaders/test_vert.spv", "/home/dlyvern/Projects/Velix/resources/shaders/test_frag.spv");
    VkPipelineShaderStageCreateInfo shaders[] = {shader.getFragmentHandler().getInfo(), shader.getVertexHandler().getInfo()};

    elix::Render render;
    render.init(shaders);

    while(window->isOpen())
    {
        window->pollEvents();
        render.drawFrame();
    }

    render.cleanup();
    vulkanContext->cleanup();
}