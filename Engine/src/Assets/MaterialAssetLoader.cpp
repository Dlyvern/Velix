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
        std::cerr << "Failed to open file: " << filePath << '\n';
        return nullptr;
    }

    nlohmann::json json;

    try
    {
        file >> json;
    }
    catch (const nlohmann::json::parse_error &e)
    {
        std::cerr << "Failed to parse material file " << e.what() << '\n';
        return nullptr;
    }

    CPUMaterial material;

    if (json.contains("texture_path"))
    {
        material.albedoTexture = json["texture_path"];

        material.name = std::filesystem::path(material.albedoTexture).filename();
    }

    if (json.contains("color"))
    {
        const auto &color = json["color"];
        material.color = glm::vec4({color[0], color[1], color[2], color[3]});
    }

    auto materialAsset = std::make_shared<MaterialAsset>(material);

    file.close();

    return materialAsset;
}

ELIX_NESTED_NAMESPACE_END