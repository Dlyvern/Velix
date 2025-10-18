#include "Engine/Render/RenderGraphPassResourceBuilder.hpp"
#include "Engine/Hash.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

std::size_t RenderGraphPassRecourceBuilder::createTexture(const RGPRDTexture& description, const ResourceUserData& userData)
{
    std::size_t data{0};

    for(const auto& c : description.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    hashing::hash(data, description.arrayLayers);
    hashing::hash(data, static_cast<uint64_t>(description.format));
    hashing::hash(data, static_cast<uint64_t>(description.initialLayout));
    hashing::hash(data, static_cast<uint64_t>(description.samples));
    hashing::hash(data, description.size.height);
    hashing::hash(data, static_cast<int>(description.size.scale));
    hashing::hash(data, description.size.width);
    hashing::hash(data, static_cast<uint16_t>(description.size.type));
    hashing::hash(data, static_cast<uint64_t>(description.usage));
    hashing::hash(data, static_cast<uint64_t>(description.aspect));

    if(m_textureHashes.find(data) == m_textureHashes.end())
        m_textureHashes[data] = description;

    if(userData.access != ResourceAccess::NONE && userData.user)
        m_textureUsers[data].push_back(userData);

    return data;
}

std::size_t RenderGraphPassRecourceBuilder::createRenderPass(const RGPRDRenderPass& renderPassDescription)
{
    std::size_t data{0};

    for(const auto& c : renderPassDescription.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    if(m_renderPasHashes.find(data) == m_renderPasHashes.end())
        m_renderPasHashes[data] = renderPassDescription;
    
    return data;
}

void RenderGraphPassRecourceBuilder::forceTextureCache(std::size_t hash, const RGPRDTexture& textureDescription)
{
    m_textureHashes[hash] = textureDescription;
}

void RenderGraphPassRecourceBuilder::forceFramebufferCache(std::size_t hash, const RGPRDFramebuffer& framebufferDescription)
{
    m_framebufferHashes[hash] = framebufferDescription;
}

std::size_t RenderGraphPassRecourceBuilder::createFramebuffer(const RGPRDFramebuffer& framebufferDescription)
{
    std::size_t data{0};

    for(const auto& c : framebufferDescription.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    for(const auto& attachmentHash : framebufferDescription.attachmentsHash)
        hashing::hash(data, attachmentHash);

    hashing::hash(data, framebufferDescription.size.height);
    hashing::hash(data, static_cast<int>(framebufferDescription.size.scale));
    hashing::hash(data, framebufferDescription.size.width);
    hashing::hash(data, static_cast<uint16_t>(framebufferDescription.size.type));
    hashing::hash(data, framebufferDescription.layers);

    if(m_framebufferHashes.find(data) == m_framebufferHashes.end())
        m_framebufferHashes[data] = framebufferDescription;

    return data;
}

std::size_t RenderGraphPassRecourceBuilder::createBuffer(const RGPRDBuffer& bufferDescription)
{
    std::size_t data{0};

    for(const auto& c : bufferDescription.name)
        hashing::hash(data, static_cast<uint8_t>(c));

    if(m_bufferHashes.find(data) == m_bufferHashes.end())
        m_bufferHashes[data] = bufferDescription;

    m_bufferHashes[data] = bufferDescription;

    return data;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDRenderPass>& RenderGraphPassRecourceBuilder::getRenderPassHashes() const
{
    return m_renderPasHashes;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDTexture>& RenderGraphPassRecourceBuilder::getTextureHashes() const
{
    return m_textureHashes;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDBuffer>& RenderGraphPassRecourceBuilder::getBufferHashes() const
{
    return m_bufferHashes;
}

const std::unordered_map<std::size_t, RenderGraphPassRecourceBuilder::RGPRDFramebuffer>& RenderGraphPassRecourceBuilder::getFramebufferHashes() const
{
    return m_framebufferHashes;
}

ELIX_NESTED_NAMESPACE_END