#ifndef ELIX_TERRAIN_ASSET_HPP
#define ELIX_TERRAIN_ASSET_HPP

#include "Core/Macros.hpp"
#include "Engine/Assets/Asset.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct TerrainLayerInfo
{
    std::string name{"Layer"};
    std::string materialPath{};
    std::string albedoTexture{};
    std::string normalTexture{};
    std::string ormTexture{};
    float uvScale{1.0f};
    float blendHardness{0.5f};
};

class TerrainAsset : public IAsset
{
public:
    static constexpr uint32_t CURRENT_VERSION = 1u;

    std::string name{"Terrain"};
    uint32_t version{CURRENT_VERSION};

    // Heightmap resolution in samples. Valid data requires width/height >= 2.
    uint32_t width{0};
    uint32_t height{0};

    // World extents in meters.
    float worldSizeX{100.0f};
    float worldSizeZ{100.0f};

    // Height scale in meters for normalized [0..1] sampled height.
    float heightScale{25.0f};

    std::string sourceHeightmapPath{};
    std::vector<uint16_t> heightSamples;

    uint32_t weightmapWidth{0};
    uint32_t weightmapHeight{0};
    uint32_t weightmapChannels{4};
    std::vector<uint8_t> weightmapData;

    std::vector<TerrainLayerInfo> layers;

    [[nodiscard]] bool isValid() const
    {
        return width >= 2u &&
               height >= 2u &&
               heightSamples.size() == static_cast<size_t>(width) * static_cast<size_t>(height);
    }

    [[nodiscard]] float sampleNormalizedHeight(uint32_t x, uint32_t y) const
    {
        if (!isValid())
            return 0.0f;

        x = std::min(x, width - 1u);
        y = std::min(y, height - 1u);
        const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
        return static_cast<float>(heightSamples[index]) / 65535.0f;
    }

    [[nodiscard]] float sampleWorldHeight(uint32_t x, uint32_t y) const
    {
        return sampleNormalizedHeight(x, y) * heightScale;
    }
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_TERRAIN_ASSET_HPP
