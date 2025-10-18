#ifndef ELIX_RENDER_GRAPH_PASS_RESOURCE_COMPILER_HPP
#define ELIX_RENDER_GRAPH_PASS_RESOURCE_COMPILER_HPP

#include "Core/Macros.hpp"
#include "Core/SwapChain.hpp"

#include "Engine/Render/RenderGraphPassResourceBuilder.hpp"
#include "Engine/Render/RenderGraphPassResourceHash.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class RenderGraphPassResourceCompiler
{
public:
    RenderGraphPassResourceCompiler(VkDevice device, VkPhysicalDevice physicalDevice, core::SwapChain::SharedPtr swapChain);

    void compile(RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage);
    void onSwapChainResize(const RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage);
private:
    VkDevice m_device{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    std::weak_ptr<core::SwapChain> m_swapChain;

    std::unordered_map<int, std::vector<std::size_t>> m_swapChainHash;

    void compileTextures(RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage);
    void compileFramebuffers(RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage);
    void compileRenderPasses(RenderGraphPassRecourceBuilder& builder, RenderGraphPassResourceHash& storage);
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_GRAPH_PASS_RESOURCE_COMPILER_HPP