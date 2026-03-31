#ifndef ELIX_ASSETS_SERIALIZER_HPP
#define ELIX_ASSETS_SERIALIZER_HPP

#include "Engine/Assets/Asset.hpp"
#include "Engine/Particles/ParticleSystem.hpp"

#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AssetsSerializer
{
public:
    std::optional<Asset::BinaryHeader> readHeader(const std::string &path) const;

    bool writeTexture(const TextureAsset &textureAsset, const std::string &outputPath) const;
    bool writeModel(const ModelAsset &modelAsset, const std::string &outputPath) const;
    bool writeAudio(const AudioAsset &audioAsset, const std::string &outputPath) const;
    bool writeAnimationAsset(const AnimationAsset &animationAsset, const std::string &outputPath) const;
    bool writeAnimationTree(const AnimationTree &tree, const std::string &outputPath) const;

    std::optional<TextureAsset> readTexture(const std::string &path) const;
    std::optional<TextureAsset> readTexture(const std::vector<uint8_t> &bytes) const;
    std::optional<ModelAsset> readModel(const std::string &path) const;
    std::optional<ModelAsset> readModel(const std::vector<uint8_t> &bytes) const;
    std::optional<AudioAsset> readAudio(const std::string &path) const;
    std::optional<AnimationAsset> readAnimationAsset(const std::string &path) const;
    std::optional<AnimationTree> readAnimationTree(const std::string &path) const;

    bool writeParticleSystem(const ParticleSystem &system, const std::string &path) const;
    std::optional<ParticleSystem::SharedPtr> readParticleSystem(const std::string &path) const;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_SERIALIZER_HPP
