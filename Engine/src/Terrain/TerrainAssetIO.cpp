#include "Engine/Terrain/TerrainAssetIO.hpp"

#include "Core/Logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>

namespace
{
    float sanitizeFinite(float value, float fallback)
    {
        return std::isfinite(value) ? value : fallback;
    }

    uint32_t sanitizeDimension(uint32_t value)
    {
        return std::clamp(value, 2u, 16385u);
    }

    uint16_t clampHeightU16(int64_t value)
    {
        return static_cast<uint16_t>(std::clamp<int64_t>(value, 0, std::numeric_limits<uint16_t>::max()));
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(engine)

bool saveTerrainAssetToFile(const TerrainAsset &terrainAsset, const std::string &filePath)
{
    if (filePath.empty())
        return false;

    std::error_code errorCode;
    const std::filesystem::path outputPath = std::filesystem::path(filePath).lexically_normal();
    if (!outputPath.parent_path().empty())
        std::filesystem::create_directories(outputPath.parent_path(), errorCode);

    if (errorCode)
    {
        VX_ENGINE_ERROR_STREAM("Failed to create terrain asset directory: " << outputPath.parent_path() << '\n');
        return false;
    }

    TerrainAsset sanitized = terrainAsset;
    sanitized.version = TerrainAsset::CURRENT_VERSION;
    sanitized.width = sanitizeDimension(sanitized.width);
    sanitized.height = sanitizeDimension(sanitized.height);
    sanitized.worldSizeX = std::max(1.0f, sanitizeFinite(sanitized.worldSizeX, 100.0f));
    sanitized.worldSizeZ = std::max(1.0f, sanitizeFinite(sanitized.worldSizeZ, 100.0f));
    sanitized.heightScale = std::max(0.01f, sanitizeFinite(sanitized.heightScale, 25.0f));
    sanitized.weightmapChannels = std::clamp(sanitized.weightmapChannels, 1u, 4u);

    const size_t expectedHeightSamples = static_cast<size_t>(sanitized.width) * static_cast<size_t>(sanitized.height);
    if (sanitized.heightSamples.size() != expectedHeightSamples)
        sanitized.heightSamples.assign(expectedHeightSamples, 0u);

    const bool validWeightmapResolution = sanitized.weightmapWidth > 0u && sanitized.weightmapHeight > 0u;
    if (!validWeightmapResolution)
    {
        sanitized.weightmapWidth = 0u;
        sanitized.weightmapHeight = 0u;
        sanitized.weightmapData.clear();
    }
    else
    {
        const size_t expectedWeightmapSize = static_cast<size_t>(sanitized.weightmapWidth) *
                                             static_cast<size_t>(sanitized.weightmapHeight) *
                                             static_cast<size_t>(sanitized.weightmapChannels);
        if (sanitized.weightmapData.size() != expectedWeightmapSize)
            sanitized.weightmapData.assign(expectedWeightmapSize, 255u);
    }

    nlohmann::json json;
    json["version"] = sanitized.version;
    json["name"] = sanitized.name;
    json["source_heightmap_path"] = sanitized.sourceHeightmapPath;

    json["heightmap"] = {
        {"width", sanitized.width},
        {"height", sanitized.height},
        {"world_size_x", sanitized.worldSizeX},
        {"world_size_z", sanitized.worldSizeZ},
        {"height_scale", sanitized.heightScale},
        {"samples_u16", sanitized.heightSamples}};

    nlohmann::json layersJson = nlohmann::json::array();
    for (const auto &layer : sanitized.layers)
    {
        layersJson.push_back({
            {"name", layer.name},
            {"material_path", layer.materialPath},
            {"albedo_texture", layer.albedoTexture},
            {"normal_texture", layer.normalTexture},
            {"orm_texture", layer.ormTexture},
            {"uv_scale", std::max(0.001f, sanitizeFinite(layer.uvScale, 1.0f))},
            {"blend_hardness", std::clamp(sanitizeFinite(layer.blendHardness, 0.5f), 0.0f, 1.0f)},
        });
    }
    json["layers"] = std::move(layersJson);

    if (validWeightmapResolution)
    {
        json["weightmap"] = {
            {"width", sanitized.weightmapWidth},
            {"height", sanitized.weightmapHeight},
            {"channels", sanitized.weightmapChannels},
            {"data_u8", sanitized.weightmapData}};
    }

    std::ofstream file(outputPath);
    if (!file.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to open terrain asset for writing: " << outputPath << '\n');
        return false;
    }

    file << std::setw(4) << json << '\n';
    return file.good();
}

std::optional<TerrainAsset> loadTerrainAssetFromFile(const std::string &filePath)
{
    if (filePath.empty())
        return std::nullopt;

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to open terrain asset: " << filePath << '\n');
        return std::nullopt;
    }

    nlohmann::json json;
    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &exception)
    {
        VX_ENGINE_ERROR_STREAM("Failed to parse terrain asset '" << filePath << "': " << exception.what() << '\n');
        return std::nullopt;
    }

    if (!json.is_object())
        return std::nullopt;

    TerrainAsset terrainAsset{};
    terrainAsset.version = static_cast<uint32_t>(json.value("version", TerrainAsset::CURRENT_VERSION));
    terrainAsset.name = json.value("name", std::string{"Terrain"});
    terrainAsset.sourceHeightmapPath = json.value("source_heightmap_path", std::string{});

    if (json.contains("heightmap") && json["heightmap"].is_object())
    {
        const auto &heightmapJson = json["heightmap"];
        terrainAsset.width = sanitizeDimension(heightmapJson.value("width", 0u));
        terrainAsset.height = sanitizeDimension(heightmapJson.value("height", 0u));
        terrainAsset.worldSizeX = std::max(1.0f, sanitizeFinite(heightmapJson.value("world_size_x", 100.0f), 100.0f));
        terrainAsset.worldSizeZ = std::max(1.0f, sanitizeFinite(heightmapJson.value("world_size_z", 100.0f), 100.0f));
        terrainAsset.heightScale = std::max(0.01f, sanitizeFinite(heightmapJson.value("height_scale", 25.0f), 25.0f));

        if (heightmapJson.contains("samples_u16") && heightmapJson["samples_u16"].is_array())
        {
            const auto &samplesJson = heightmapJson["samples_u16"];
            terrainAsset.heightSamples.reserve(samplesJson.size());
            for (const auto &sampleJson : samplesJson)
            {
                if (sampleJson.is_number_unsigned())
                    terrainAsset.heightSamples.push_back(static_cast<uint16_t>(std::min<uint64_t>(sampleJson.get<uint64_t>(), std::numeric_limits<uint16_t>::max())));
                else if (sampleJson.is_number_integer())
                    terrainAsset.heightSamples.push_back(clampHeightU16(sampleJson.get<int64_t>()));
                else
                    terrainAsset.heightSamples.push_back(0u);
            }
        }
    }

    const size_t expectedHeightSamples = static_cast<size_t>(terrainAsset.width) * static_cast<size_t>(terrainAsset.height);
    if (expectedHeightSamples == 0u)
        return std::nullopt;

    if (terrainAsset.heightSamples.size() != expectedHeightSamples)
        terrainAsset.heightSamples.assign(expectedHeightSamples, 0u);

    terrainAsset.layers.clear();
    if (json.contains("layers") && json["layers"].is_array())
    {
        for (const auto &layerJson : json["layers"])
        {
            if (!layerJson.is_object())
                continue;

            TerrainLayerInfo layer{};
            layer.name = layerJson.value("name", std::string{"Layer"});
            layer.materialPath = layerJson.value("material_path", std::string{});
            layer.albedoTexture = layerJson.value("albedo_texture", std::string{});
            layer.normalTexture = layerJson.value("normal_texture", std::string{});
            layer.ormTexture = layerJson.value("orm_texture", std::string{});
            layer.uvScale = std::max(0.001f, sanitizeFinite(layerJson.value("uv_scale", 1.0f), 1.0f));
            layer.blendHardness = std::clamp(sanitizeFinite(layerJson.value("blend_hardness", 0.5f), 0.5f), 0.0f, 1.0f);
            terrainAsset.layers.push_back(std::move(layer));
        }
    }

    if (json.contains("weightmap") && json["weightmap"].is_object())
    {
        const auto &weightmapJson = json["weightmap"];
        terrainAsset.weightmapWidth = weightmapJson.value("width", 0u);
        terrainAsset.weightmapHeight = weightmapJson.value("height", 0u);
        terrainAsset.weightmapChannels = std::clamp(weightmapJson.value("channels", 4u), 1u, 4u);

        if (weightmapJson.contains("data_u8") && weightmapJson["data_u8"].is_array())
        {
            const auto &dataJson = weightmapJson["data_u8"];
            terrainAsset.weightmapData.reserve(dataJson.size());
            for (const auto &valueJson : dataJson)
            {
                if (valueJson.is_number_unsigned())
                    terrainAsset.weightmapData.push_back(static_cast<uint8_t>(std::min<uint64_t>(valueJson.get<uint64_t>(), 255u)));
                else if (valueJson.is_number_integer())
                    terrainAsset.weightmapData.push_back(static_cast<uint8_t>(std::clamp<int64_t>(valueJson.get<int64_t>(), 0, 255)));
                else
                    terrainAsset.weightmapData.push_back(0u);
            }
        }

        const size_t expectedWeightmapSize = static_cast<size_t>(terrainAsset.weightmapWidth) *
                                             static_cast<size_t>(terrainAsset.weightmapHeight) *
                                             static_cast<size_t>(terrainAsset.weightmapChannels);
        if (expectedWeightmapSize == 0u || terrainAsset.weightmapData.size() != expectedWeightmapSize)
        {
            terrainAsset.weightmapWidth = 0u;
            terrainAsset.weightmapHeight = 0u;
            terrainAsset.weightmapData.clear();
        }
    }

    return terrainAsset;
}

ELIX_NESTED_NAMESPACE_END
