#include "Engine/Assets/MaterialAssetLoader.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <fstream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

const std::vector<std::string> MaterialAssetLoader::getSupportedFormats() const
{
    return {".elixmat"};
}

std::shared_ptr<IAsset> MaterialAssetLoader::load(const std::string &filePath)
{
    std::ifstream file(filePath);

    if (!file.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to open file: " << filePath << '\n');
        return nullptr;
    }

    nlohmann::json json;

    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        VX_ENGINE_ERROR_STREAM("Failed to parse material file " << e.what() << '\n');
        return nullptr;
    }

    CPUMaterial material;
    bool hasMetallic = false;
    bool hasRoughness = false;
    bool hasAoStrength = false;
    bool hasNormalScale = false;

    if (json.contains("name") && json["name"].is_string())
        material.name = json["name"].get<std::string>();

    if (json.contains("texture_path"))
    {
        material.albedoTexture = json["texture_path"];

        // material.name = std::filesystem::path(material.albedoTexture).filename();
    }

    if (json.contains("normal_texture") && json["normal_texture"].is_string())
        material.normalTexture = json["normal_texture"].get<std::string>();

    if (json.contains("orm_texture") && json["orm_texture"].is_string())
        material.ormTexture = json["orm_texture"].get<std::string>();

    if (json.contains("emissive_texture") && json["emissive_texture"].is_string())
        material.emissiveTexture = json["emissive_texture"].get<std::string>();

    if (json.contains("color"))
    {
        const auto &color = json["color"];
        if (color.is_array() && color.size() >= 4)
            material.baseColorFactor = glm::vec4({color[0], color[1], color[2], color[3]});
    }

    if (json.contains("emissive"))
    {
        const auto &emissive = json["emissive"];
        if (emissive.is_array() && emissive.size() >= 3)
            material.emissiveFactor = glm::vec3({emissive[0], emissive[1], emissive[2]});
    }

    if (json.contains("metallic"))
    {
        material.metallicFactor = json["metallic"].get<float>();
        hasMetallic = true;
    }

    if (json.contains("roughness"))
    {
        material.roughnessFactor = json["roughness"].get<float>();
        hasRoughness = true;
    }

    if (json.contains("ao_strength"))
    {
        material.aoStrength = json["ao_strength"].get<float>();
        hasAoStrength = true;
    }

    if (json.contains("normal_scale"))
    {
        material.normalScale = json["normal_scale"].get<float>();
        hasNormalScale = true;
    }

    if (json.contains("ior"))
        material.ior = json["ior"].get<float>();

    if (json.contains("alpha_cutoff"))
        material.alphaCutoff = json["alpha_cutoff"].get<float>();

    if (json.contains("flags"))
        material.flags = json["flags"].get<uint32_t>();

    if (json.contains("uv_scale"))
    {
        const auto &uvScale = json["uv_scale"];
        if (uvScale.is_array() && uvScale.size() >= 2)
            material.uvScale = glm::vec2({uvScale[0], uvScale[1]});
    }

    if (json.contains("uv_location"))
    {
        const auto &uvLocation = json["uv_location"];
        if (uvLocation.is_array() && uvLocation.size() >= 2)
            material.uvOffset = glm::vec2({uvLocation[0], uvLocation[1]});
    }
    else if (json.contains("uv_offset"))
    {
        const auto &uvOffset = json["uv_offset"];
        if (uvOffset.is_array() && uvOffset.size() >= 2)
            material.uvOffset = glm::vec2({uvOffset[0], uvOffset[1]});
    }

    if (json.contains("uv_rotation"))
        material.uvRotation = json["uv_rotation"].get<float>();

    auto sanitizeFinite = [](float value, float fallback) -> float
    {
        return std::isfinite(value) ? value : fallback;
    };

    material.metallicFactor = std::clamp(sanitizeFinite(material.metallicFactor, 0.0f), 0.0f, 1.0f);
    material.roughnessFactor = std::clamp(sanitizeFinite(material.roughnessFactor, 1.0f), 0.04f, 1.0f);
    material.aoStrength = std::clamp(sanitizeFinite(material.aoStrength, 1.0f), 0.0f, 1.0f);
    material.normalScale = std::max(0.0f, sanitizeFinite(material.normalScale, 1.0f));
    material.ior = std::clamp(sanitizeFinite(material.ior, 1.5f), 1.0f, 2.6f);
    material.alphaCutoff = std::clamp(sanitizeFinite(material.alphaCutoff, 0.5f), 0.0f, 1.0f);
    material.uvScale.x = sanitizeFinite(material.uvScale.x, 1.0f);
    material.uvScale.y = sanitizeFinite(material.uvScale.y, 1.0f);
    material.uvOffset.x = sanitizeFinite(material.uvOffset.x, 0.0f);
    material.uvOffset.y = sanitizeFinite(material.uvOffset.y, 0.0f);
    material.uvRotation = sanitizeFinite(material.uvRotation, 0.0f);

    const uint32_t legacyGlassFlag = engine::Material::MaterialFlags::EMATERIAL_FLAG_LEGACY_GLASS;
    if ((material.flags & legacyGlassFlag) != 0u)
    {
        material.flags |= engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND;
        material.flags &= ~legacyGlassFlag;
    }

    const uint32_t supportedFlags =
        engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK |
        engine::Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND |
        engine::Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED |
        engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_V |
        engine::Material::MaterialFlags::EMATERIAL_FLAG_FLIP_U |
        engine::Material::MaterialFlags::EMATERIAL_FLAG_CLAMP_UV;
    material.flags &= supportedFlags;

    // Keep non-ORM materials dielectric unless author explicitly set values.
    if (material.ormTexture.empty() && !hasMetallic)
        material.metallicFactor = 0.0f;
    if (material.ormTexture.empty() && !hasRoughness)
        material.roughnessFactor = 1.0f;
    if (material.ormTexture.empty() && !hasAoStrength)
        material.aoStrength = 1.0f;
    if (!hasNormalScale)
        material.normalScale = 1.0f;

    auto materialAsset = std::make_shared<MaterialAsset>(material);

    file.close();

    return materialAsset;
}

ELIX_NESTED_NAMESPACE_END
