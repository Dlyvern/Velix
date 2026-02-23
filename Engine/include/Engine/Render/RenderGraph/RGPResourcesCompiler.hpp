#ifndef ELIX_RGP_RESOURCES_COMPILER_HPP
#define ELIX_RGP_RESOURCES_COMPILER_HPP

#include "Engine/Render/RenderGraph/RGPResourcesBuilder.hpp"
#include "Engine/Render/RenderGraph/RGPResourcesStorage.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class RGPResourcesCompiler
{
public:
    std::vector<VkImageMemoryBarrier2> compile(RGPResourcesBuilder &builder, RGPResourcesStorage &storage);
    std::vector<VkImageMemoryBarrier2> compile(const std::vector<RGPResourceHandler> &resourcesIds, RGPResourcesBuilder &builder, RGPResourcesStorage &storage);

    std::vector<VkImageMemoryBarrier2> onSwapChainResized(RGPResourcesBuilder &builder, RGPResourcesStorage &storage);
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RGP_RESOURCES_COMPILER_HPP