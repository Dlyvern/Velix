#ifndef ELIX_RENDER_GRAPH_PASS_RESOURCE_BUILDER_HPP
#define ELIX_RENDER_GRAPH_PASS_RESOURCE_BUILDER_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/RenderGraphPassResourceTypes.hpp"

#include <volk.h>

#include <functional>
#include <unordered_map>
#include <cstddef>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class ResourceAccess : uint8_t
{
    WRITE = 0,
    READ = 1,
    NONE = 2
};

struct ResourceUserData
{
    ResourceAccess access{ResourceAccess::NONE};
    void* user{nullptr};
};

class RenderGraphPassRecourceBuilder
{
public:
    using RGPRDTexture = RenderGraphPassResourceTypes::TextureDescription;
    using RGPRDBuffer = RenderGraphPassResourceTypes::BufferDescription;
    using RGPRDFramebuffer = RenderGraphPassResourceTypes::FramebufferDescription;
    using RGPRDRenderPass = RenderGraphPassResourceTypes::RenderPassDescription;

    std::size_t createTexture(const RGPRDTexture& textureDescription, const ResourceUserData& userData = {});
    std::size_t createBuffer(const RGPRDBuffer& bufferDescription);
    std::size_t createFramebuffer(const RGPRDFramebuffer& framebufferDescription);
    std::size_t createRenderPass(const RGPRDRenderPass& renderPassDescription);

    void forceTextureCache(std::size_t hash, const RGPRDTexture& textureDescription);
    void forceFramebufferCache(std::size_t, const RGPRDFramebuffer& framebufferDescription);

    const std::unordered_map<std::size_t, RGPRDTexture>& getTextureHashes() const;
    const std::unordered_map<std::size_t, RGPRDBuffer>& getBufferHashes() const;
    const std::unordered_map<std::size_t, RGPRDFramebuffer>& getFramebufferHashes() const;
    const std::unordered_map<std::size_t, RGPRDRenderPass>& getRenderPassHashes() const;
private:
    std::unordered_map<std::size_t, RGPRDTexture> m_textureHashes;
    std::unordered_map<std::size_t, std::vector<ResourceUserData>> m_textureUsers;

    std::unordered_map<std::size_t, RGPRDRenderPass> m_renderPasHashes;
    std::unordered_map<std::size_t, RGPRDBuffer> m_bufferHashes;
    std::unordered_map<std::size_t, RGPRDFramebuffer> m_framebufferHashes;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_GRAPH_PASS_RESOURCE_BUILDER_HPP