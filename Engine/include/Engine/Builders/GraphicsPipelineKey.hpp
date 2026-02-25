#ifndef ELIX_GRAPHICS_PIPELINE_KEY_HPP
#define ELIX_GRAPHICS_PIPELINE_KEY_HPP

#include "Core/Macros.hpp"
#include "Engine/Caches/Hash.hpp"

#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class ShaderId : uint8_t
{
    StaticShadow,
    SkinnedShadow,
    PreviewMesh,
    SkyboxHDR,
    Skybox,
    SkyLight,
    ToneMap,
    SelectionOverlay,
    Present,
    GBufferStatic,
    GBufferSkinned,
    Lighting,
    None
};

enum class RenderQueue : uint8_t
{
    Opaque,
    Transparent,
    Overlay
};

enum class BlendMode : uint8_t
{
    None,
    AlphaBlend
};

enum class CullMode : uint8_t
{
    Back,
    None,
    Front
};

struct GraphicsPipelineKey
{
    ShaderId shader{ShaderId::None};
    BlendMode blend = BlendMode::None;
    CullMode cull{CullMode::Back};
    bool depthTest{true};
    bool depthWrite{true};
    VkCompareOp depthCompare{VK_COMPARE_OP_LESS};
    VkPolygonMode polygonMode{VK_POLYGON_MODE_FILL};
    VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};

    std::vector<VkFormat> colorFormats;
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};

    bool operator==(const GraphicsPipelineKey &o) const noexcept
    {
        return shader == o.shader &&
               blend == o.blend &&
               cull == o.cull &&
               pipelineLayout == o.pipelineLayout &&
               depthTest == o.depthTest &&
               depthWrite == o.depthWrite &&
               depthCompare == o.depthCompare &&
               polygonMode == o.polygonMode &&
               topology == o.topology &&
               colorFormats == o.colorFormats &&
               depthFormat == o.depthFormat;
    }
};

struct GraphicsPipelineKeyHash
{
    size_t operator()(const GraphicsPipelineKey &k) const noexcept
    {
        size_t data = 0;

        hashing::hash(data, static_cast<uint8_t>(k.blend));
        hashing::hash(data, static_cast<uint8_t>(k.shader));
        hashing::hash(data, static_cast<uint8_t>(k.cull));
        hashing::hash(data, static_cast<bool>(k.depthTest));
        hashing::hash(data, static_cast<bool>(k.depthWrite));
        hashing::hash(data, static_cast<uint32_t>(k.depthCompare));
        hashing::hash(data, static_cast<uint32_t>(k.polygonMode));
        hashing::hash(data, static_cast<uint32_t>(k.topology));
        // hashing::hash(data, static_cast<uint32_t>(k.pipelineLayout));

        for (const auto &colorFormat : k.colorFormats)
            hashing::hash(data, static_cast<uint32_t>(colorFormat));

        hashing::hash(data, static_cast<uint32_t>(k.depthFormat));

        return data;
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GRAPHICS_PIPELINE_KEY_HPP
