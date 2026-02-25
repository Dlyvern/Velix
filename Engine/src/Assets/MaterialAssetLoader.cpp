#include "Engine/Assets/MaterialAssetLoader.hpp"

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
        material.metallicFactor = json["metallic"].get<float>();

    if (json.contains("roughness"))
        material.roughnessFactor = json["roughness"].get<float>();

    if (json.contains("ao_strength"))
        material.aoStrength = json["ao_strength"].get<float>();

    if (json.contains("normal_scale"))
        material.normalScale = json["normal_scale"].get<float>();

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

    if (json.contains("uv_offset"))
    {
        const auto &uvOffset = json["uv_offset"];
        if (uvOffset.is_array() && uvOffset.size() >= 2)
            material.uvOffset = glm::vec2({uvOffset[0], uvOffset[1]});
    }

    auto materialAsset = std::make_shared<MaterialAsset>(material);

    file.close();

    return materialAsset;
}

ELIX_NESTED_NAMESPACE_END
