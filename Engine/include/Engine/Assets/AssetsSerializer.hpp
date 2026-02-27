#ifndef ELIX_ASSETS_SERIALIZER_HPP
#define ELIX_ASSETS_SERIALIZER_HPP

#include "Engine/Assets/Asset.hpp"

#include <optional>
#include <unordered_set>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AssetsSerializer
{
public:
    std::optional<Asset::BinaryHeader> readHeader(const std::string &path) const;

    bool writeTexture(const TextureAsset &textureAsset, const std::string &outputPath) const;
    bool writeModel(const ModelAsset &modelAsset, const std::string &outputPath) const;

    std::optional<TextureAsset> readTexture(const std::string &path) const;
    std::optional<ModelAsset> readModel(const std::string &path) const;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_SERIALIZER_HPP
