#include "Engine/Assets/AssetsSerializer.hpp"
#include "Engine/Assets/Compressor.hpp"

#include "Core/Logger.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace
{
    template <typename T>
    bool writePOD(std::ostream &stream, const T &value)
    {
        stream.write(reinterpret_cast<const char *>(&value), sizeof(T));
        return stream.good();
    }

    template <typename T>
    bool readPOD(std::istream &stream, T &value)
    {
        stream.read(reinterpret_cast<char *>(&value), sizeof(T));
        return stream.good();
    }

    bool writeString(std::ostream &stream, const std::string &value)
    {
        const uint32_t size = static_cast<uint32_t>(value.size());
        if (!writePOD(stream, size))
            return false;

        if (size == 0u)
            return true;

        stream.write(value.data(), static_cast<std::streamsize>(size));
        return stream.good();
    }

    bool readString(std::istream &stream, std::string &outValue)
    {
        constexpr uint32_t MAX_STRING_SIZE = 16u * 1024u * 1024u;

        uint32_t size = 0u;
        if (!readPOD(stream, size))
            return false;

        if (size > MAX_STRING_SIZE)
            return false;

        outValue.resize(size);
        if (size == 0u)
            return true;

        stream.read(outValue.data(), static_cast<std::streamsize>(size));
        return stream.good();
    }

    template <typename T>
    bool writeVector(std::ostream &stream, const std::vector<T> &value)
    {
        const uint64_t size = static_cast<uint64_t>(value.size());
        if (!writePOD(stream, size))
            return false;

        if (size == 0u)
            return true;

        stream.write(reinterpret_cast<const char *>(value.data()), static_cast<std::streamsize>(sizeof(T) * value.size()));
        return stream.good();
    }

    template <typename T>
    bool readVector(std::istream &stream, std::vector<T> &outValue, uint64_t maxElements = (1ull << 28))
    {
        uint64_t size = 0u;
        if (!readPOD(stream, size))
            return false;

        if (size > maxElements)
            return false;

        outValue.resize(static_cast<size_t>(size));
        if (size == 0u)
            return true;

        stream.read(reinterpret_cast<char *>(outValue.data()), static_cast<std::streamsize>(sizeof(T) * outValue.size()));
        return stream.good();
    }

    bool writeBytes(std::ostream &stream, const std::vector<uint8_t> &bytes)
    {
        return writeVector<uint8_t>(stream, bytes);
    }

    bool readBytes(std::istream &stream, std::vector<uint8_t> &outBytes)
    {
        return readVector<uint8_t>(stream, outBytes, 1ull << 31);
    }

    bool writeHeader(std::ostream &stream, elix::engine::Asset::AssetType type, uint64_t payloadSize, uint8_t reserved0 = 0u)
    {
        elix::engine::Asset::BinaryHeader header{};
        std::memcpy(header.magic, elix::engine::Asset::MAGIC.data(), elix::engine::Asset::MAGIC.size());
        header.version = elix::engine::Asset::VERSION;
        header.type = static_cast<uint8_t>(type);
        header.reserved[0] = reserved0;
        header.reserved[1] = 0u;
        header.reserved[2] = 0u;
        header.payloadSize = payloadSize;

        return writePOD(stream, header);
    }

    bool readHeader(std::istream &stream, elix::engine::Asset::BinaryHeader &outHeader)
    {
        if (!readPOD(stream, outHeader))
            return false;

        if (std::memcmp(outHeader.magic, elix::engine::Asset::MAGIC.data(), elix::engine::Asset::MAGIC.size()) != 0)
            return false;

        if (outHeader.version != elix::engine::Asset::VERSION)
            return false;

        return true;
    }

    bool writeMaterial(std::ostream &stream, const elix::engine::CPUMaterial &material)
    {
        return writePOD(stream, material.flags) &&
               writeString(stream, material.albedoTexture) &&
               writeString(stream, material.normalTexture) &&
               writeString(stream, material.ormTexture) &&
               writeString(stream, material.emissiveTexture) &&
               writeString(stream, material.name) &&
               writePOD(stream, material.baseColorFactor) &&
               writePOD(stream, material.emissiveFactor) &&
               writePOD(stream, material.metallicFactor) &&
               writePOD(stream, material.roughnessFactor) &&
               writePOD(stream, material.aoStrength) &&
               writePOD(stream, material.normalScale) &&
               writePOD(stream, material.alphaCutoff) &&
               writePOD(stream, material.uvScale) &&
               writePOD(stream, material.uvOffset);
    }

    bool readMaterial(std::istream &stream, elix::engine::CPUMaterial &outMaterial)
    {
        return readPOD(stream, outMaterial.flags) &&
               readString(stream, outMaterial.albedoTexture) &&
               readString(stream, outMaterial.normalTexture) &&
               readString(stream, outMaterial.ormTexture) &&
               readString(stream, outMaterial.emissiveTexture) &&
               readString(stream, outMaterial.name) &&
               readPOD(stream, outMaterial.baseColorFactor) &&
               readPOD(stream, outMaterial.emissiveFactor) &&
               readPOD(stream, outMaterial.metallicFactor) &&
               readPOD(stream, outMaterial.roughnessFactor) &&
               readPOD(stream, outMaterial.aoStrength) &&
               readPOD(stream, outMaterial.normalScale) &&
               readPOD(stream, outMaterial.alphaCutoff) &&
               readPOD(stream, outMaterial.uvScale) &&
               readPOD(stream, outMaterial.uvOffset);
    }

    bool writeSkeleton(std::ostream &stream, const std::optional<elix::engine::Skeleton> &skeletonOptional)
    {
        const uint8_t hasSkeleton = skeletonOptional.has_value() ? 1u : 0u;
        if (!writePOD(stream, hasSkeleton))
            return false;

        if (!skeletonOptional.has_value())
            return true;

        auto &skeleton = const_cast<elix::engine::Skeleton &>(skeletonOptional.value());
        const uint32_t bonesCount = static_cast<uint32_t>(skeleton.getBonesCount());

        if (!writePOD(stream, bonesCount))
            return false;

        if (!writePOD(stream, skeleton.globalInverseTransform))
            return false;

        for (uint32_t boneIndex = 0; boneIndex < bonesCount; ++boneIndex)
        {
            auto *bone = skeleton.getBone(static_cast<int>(boneIndex));
            if (!bone)
                return false;

            if (!writeString(stream, bone->name) ||
                !writePOD(stream, bone->id) ||
                !writePOD(stream, bone->parentId) ||
                !writePOD(stream, bone->offsetMatrix) ||
                !writePOD(stream, bone->finalTransformation) ||
                !writePOD(stream, bone->localBindTransform) ||
                !writePOD(stream, bone->globalBindTransform))
                return false;

            const uint32_t childrenCount = static_cast<uint32_t>(bone->children.size());
            if (!writePOD(stream, childrenCount))
                return false;

            for (const auto childId : bone->children)
                if (!writePOD(stream, childId))
                    return false;
        }

        return true;
    }

    bool readSkeleton(std::istream &stream, std::optional<elix::engine::Skeleton> &outSkeleton)
    {
        uint8_t hasSkeleton = 0u;
        if (!readPOD(stream, hasSkeleton))
            return false;

        if (hasSkeleton == 0u)
        {
            outSkeleton = std::nullopt;
            return true;
        }

        uint32_t bonesCount = 0u;
        if (!readPOD(stream, bonesCount))
            return false;

        if (bonesCount > (1u << 16))
            return false;

        elix::engine::Skeleton skeleton;
        if (!readPOD(stream, skeleton.globalInverseTransform))
            return false;

        std::vector<std::vector<int>> childrenByBone;
        childrenByBone.resize(bonesCount);

        for (uint32_t boneIndex = 0; boneIndex < bonesCount; ++boneIndex)
        {
            elix::engine::Skeleton::BoneInfo bone{};

            if (!readString(stream, bone.name) ||
                !readPOD(stream, bone.id) ||
                !readPOD(stream, bone.parentId) ||
                !readPOD(stream, bone.offsetMatrix) ||
                !readPOD(stream, bone.finalTransformation) ||
                !readPOD(stream, bone.localBindTransform) ||
                !readPOD(stream, bone.globalBindTransform))
                return false;

            uint32_t childrenCount = 0u;
            if (!readPOD(stream, childrenCount))
                return false;

            if (childrenCount > (1u << 16))
                return false;

            bone.children.resize(childrenCount);
            for (uint32_t childIndex = 0; childIndex < childrenCount; ++childIndex)
                if (!readPOD(stream, bone.children[childIndex]))
                    return false;

            childrenByBone[boneIndex] = bone.children;
            skeleton.addBone(bone);
        }

        for (uint32_t boneIndex = 0; boneIndex < bonesCount; ++boneIndex)
        {
            auto *bone = skeleton.getBone(static_cast<int>(boneIndex));
            if (!bone)
                continue;

            bone->children = childrenByBone[boneIndex];
        }

        outSkeleton = skeleton;
        return true;
    }

    bool writeAnimations(std::ostream &stream, const std::vector<elix::engine::Animation> &animations)
    {
        const uint32_t animationsCount = static_cast<uint32_t>(animations.size());
        if (!writePOD(stream, animationsCount))
            return false;

        for (const auto &animation : animations)
        {
            if (!writeString(stream, animation.name) ||
                !writePOD(stream, animation.ticksPerSecond) ||
                !writePOD(stream, animation.duration))
                return false;

            const uint32_t trackCount = static_cast<uint32_t>(animation.boneAnimations.size());
            if (!writePOD(stream, trackCount))
                return false;

            for (const auto &track : animation.boneAnimations)
            {
                if (!writeString(stream, track.objectName))
                    return false;

                const uint32_t keyFramesCount = static_cast<uint32_t>(track.keyFrames.size());
                if (!writePOD(stream, keyFramesCount))
                    return false;

                for (const auto &keyFrame : track.keyFrames)
                    if (!writePOD(stream, keyFrame.rotation) ||
                        !writePOD(stream, keyFrame.position) ||
                        !writePOD(stream, keyFrame.scale) ||
                        !writePOD(stream, keyFrame.timeStamp))
                        return false;
            }
        }

        return true;
    }

    bool readAnimations(std::istream &stream, std::vector<elix::engine::Animation> &outAnimations)
    {
        uint32_t animationsCount = 0u;
        if (!readPOD(stream, animationsCount))
            return false;

        if (animationsCount > (1u << 16))
            return false;

        outAnimations.clear();
        outAnimations.reserve(animationsCount);

        for (uint32_t animationIndex = 0; animationIndex < animationsCount; ++animationIndex)
        {
            elix::engine::Animation animation{};
            if (!readString(stream, animation.name) ||
                !readPOD(stream, animation.ticksPerSecond) ||
                !readPOD(stream, animation.duration))
                return false;

            uint32_t trackCount = 0u;
            if (!readPOD(stream, trackCount))
                return false;

            if (trackCount > (1u << 16))
                return false;

            animation.boneAnimations.resize(trackCount);
            for (uint32_t trackIndex = 0; trackIndex < trackCount; ++trackIndex)
            {
                auto &track = animation.boneAnimations[trackIndex];
                if (!readString(stream, track.objectName))
                    return false;

                uint32_t keyFramesCount = 0u;
                if (!readPOD(stream, keyFramesCount))
                    return false;

                if (keyFramesCount > (1u << 20))
                    return false;

                track.keyFrames.resize(keyFramesCount);
                for (uint32_t keyFrameIndex = 0; keyFrameIndex < keyFramesCount; ++keyFrameIndex)
                {
                    auto &keyFrame = track.keyFrames[keyFrameIndex];
                    if (!readPOD(stream, keyFrame.rotation) ||
                        !readPOD(stream, keyFrame.position) ||
                        !readPOD(stream, keyFrame.scale) ||
                        !readPOD(stream, keyFrame.timeStamp))
                        return false;
                }
            }

            outAnimations.push_back(std::move(animation));
        }

        return true;
    }

    std::optional<uint64_t> computeUncompressedTextureSize(const elix::engine::TextureAsset &textureAsset)
    {
        const uint64_t width = static_cast<uint64_t>(textureAsset.width);
        const uint64_t height = static_cast<uint64_t>(textureAsset.height);

        if (width == 0u || height == 0u)
            return 0u;

        if (width > (std::numeric_limits<uint64_t>::max() / height))
            return std::nullopt;

        const uint64_t texelsCount = width * height;

        switch (textureAsset.encoding)
        {
        case elix::engine::TextureAsset::PixelEncoding::RGBA8:
            if (texelsCount > (std::numeric_limits<uint64_t>::max() / 4u))
                return std::nullopt;
            return texelsCount * 4u;
        case elix::engine::TextureAsset::PixelEncoding::RGBA32F:
            if (texelsCount > (std::numeric_limits<uint64_t>::max() / (4u * sizeof(float))))
                return std::nullopt;
            return texelsCount * 4u * sizeof(float);
        case elix::engine::TextureAsset::PixelEncoding::COMPRESSED_GPU:
        default:
            return std::nullopt;
        }
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(engine)

std::optional<Asset::BinaryHeader> AssetsSerializer::readHeader(const std::string &path) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return std::nullopt;

    Asset::BinaryHeader header{};
    if (!::readHeader(stream, header))
        return std::nullopt;

    return header;
}

bool AssetsSerializer::writeTexture(const TextureAsset &textureAsset, const std::string &outputPath) const
{
    std::vector<uint8_t> storedPixels = textureAsset.pixels;
    uint8_t compressionAlgorithm = static_cast<uint8_t>(Compressor::Algorithm::None);

    if (!textureAsset.pixels.empty() && textureAsset.encoding != TextureAsset::PixelEncoding::COMPRESSED_GPU)
    {
        std::vector<uint8_t> compressedPixels;
        if (Compressor::compress(textureAsset.pixels, compressedPixels, Compressor::Algorithm::Deflate, 6))
        {
            // Keep raw data when compression gain is too small.
            if (compressedPixels.size() + 32u < textureAsset.pixels.size())
            {
                storedPixels = std::move(compressedPixels);
                compressionAlgorithm = static_cast<uint8_t>(Compressor::Algorithm::Deflate);
            }
        }
    }

    std::ostringstream payloadStream(std::ios::binary);
    if (!writeString(payloadStream, textureAsset.name) ||
        !writeString(payloadStream, textureAsset.sourcePath) ||
        !writeString(payloadStream, textureAsset.assetPath) ||
        !writePOD(payloadStream, textureAsset.width) ||
        !writePOD(payloadStream, textureAsset.height) ||
        !writePOD(payloadStream, textureAsset.channels))
        return false;

    const uint8_t encoding = static_cast<uint8_t>(textureAsset.encoding);
    if (!writePOD(payloadStream, encoding) ||
        !writePOD(payloadStream, textureAsset.vkFormat) ||
        !writeBytes(payloadStream, storedPixels))
        return false;

    const std::string payload = payloadStream.str();

    std::error_code directoryError;
    const auto outputFilesystemPath = std::filesystem::path(outputPath).lexically_normal();
    const auto parentPath = outputFilesystemPath.parent_path();
    if (!parentPath.empty())
        std::filesystem::create_directories(parentPath, directoryError);

    std::ofstream stream(outputFilesystemPath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to open texture asset output file: " << outputPath << '\n');
        return false;
    }

    if (!writeHeader(stream, Asset::AssetType::TEXTURE, static_cast<uint64_t>(payload.size()), compressionAlgorithm))
        return false;

    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return stream.good();
}

bool AssetsSerializer::writeModel(const ModelAsset &modelAsset, const std::string &outputPath) const
{
    std::ostringstream payloadStream(std::ios::binary);
    if (!writeString(payloadStream, modelAsset.sourcePath) ||
        !writeString(payloadStream, modelAsset.assetPath))
        return false;

    const uint32_t meshesCount = static_cast<uint32_t>(modelAsset.meshes.size());
    if (!writePOD(payloadStream, meshesCount))
        return false;

    for (const auto &mesh : modelAsset.meshes)
    {
        if (!writeString(payloadStream, mesh.name) ||
            !writeVector(payloadStream, mesh.vertexData) ||
            !writeVector(payloadStream, mesh.indices) ||
            !writePOD(payloadStream, mesh.vertexStride) ||
            !writePOD(payloadStream, mesh.vertexLayoutHash) ||
            !writeMaterial(payloadStream, mesh.material) ||
            !writePOD(payloadStream, mesh.localTransform))
            return false;
    }

    if (!writeSkeleton(payloadStream, modelAsset.skeleton))
        return false;

    if (!writeAnimations(payloadStream, modelAsset.animations))
        return false;

    const std::string payload = payloadStream.str();

    std::error_code directoryError;
    const auto outputFilesystemPath = std::filesystem::path(outputPath).lexically_normal();
    const auto parentPath = outputFilesystemPath.parent_path();
    if (!parentPath.empty())
        std::filesystem::create_directories(parentPath, directoryError);

    std::ofstream stream(outputFilesystemPath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to open model asset output file: " << outputPath << '\n');
        return false;
    }

    if (!writeHeader(stream, Asset::AssetType::MODEL, static_cast<uint64_t>(payload.size())))
        return false;

    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return stream.good();
}

std::optional<TextureAsset> AssetsSerializer::readTexture(const std::string &path) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return std::nullopt;

    Asset::BinaryHeader header{};
    if (!::readHeader(stream, header))
        return std::nullopt;

    if (static_cast<Asset::AssetType>(header.type) != Asset::AssetType::TEXTURE)
        return std::nullopt;

    TextureAsset textureAsset{};
    uint8_t encoding = 0u;

    if (!readString(stream, textureAsset.name) ||
        !readString(stream, textureAsset.sourcePath) ||
        !readString(stream, textureAsset.assetPath) ||
        !readPOD(stream, textureAsset.width) ||
        !readPOD(stream, textureAsset.height) ||
        !readPOD(stream, textureAsset.channels) ||
        !readPOD(stream, encoding) ||
        !readPOD(stream, textureAsset.vkFormat) ||
        !readBytes(stream, textureAsset.pixels))
        return std::nullopt;

    textureAsset.encoding = static_cast<TextureAsset::PixelEncoding>(encoding);

    const auto compressionAlgorithm = static_cast<Compressor::Algorithm>(header.reserved[0]);
    if (compressionAlgorithm != Compressor::Algorithm::None)
    {
        const auto expectedSize = computeUncompressedTextureSize(textureAsset);
        if (!expectedSize.has_value())
        {
            VX_ENGINE_ERROR_STREAM("Failed to decode compressed texture payload (unsupported encoding): " << path << '\n');
            return std::nullopt;
        }

        std::vector<uint8_t> decompressedPixels;
        if (!Compressor::decompress(textureAsset.pixels, static_cast<size_t>(expectedSize.value()), decompressedPixels, compressionAlgorithm))
        {
            VX_ENGINE_ERROR_STREAM("Failed to decompress texture payload: " << path << '\n');
            return std::nullopt;
        }

        textureAsset.pixels = std::move(decompressedPixels);
    }

    if (textureAsset.assetPath.empty())
        textureAsset.assetPath = std::filesystem::path(path).lexically_normal().string();

    return textureAsset;
}

std::optional<ModelAsset> AssetsSerializer::readModel(const std::string &path) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return std::nullopt;

    Asset::BinaryHeader header{};
    if (!::readHeader(stream, header))
        return std::nullopt;

    if (static_cast<Asset::AssetType>(header.type) != Asset::AssetType::MODEL)
        return std::nullopt;

    ModelAsset modelAsset{{}, std::nullopt, {}};
    if (!readString(stream, modelAsset.sourcePath) ||
        !readString(stream, modelAsset.assetPath))
        return std::nullopt;

    uint32_t meshesCount = 0u;
    if (!readPOD(stream, meshesCount))
        return std::nullopt;

    if (meshesCount > (1u << 16))
        return std::nullopt;

    modelAsset.meshes.resize(meshesCount);
    for (uint32_t meshIndex = 0; meshIndex < meshesCount; ++meshIndex)
    {
        auto &mesh = modelAsset.meshes[meshIndex];
        if (!readString(stream, mesh.name) ||
            !readVector(stream, mesh.vertexData, 1ull << 31) ||
            !readVector(stream, mesh.indices, 1ull << 31) ||
            !readPOD(stream, mesh.vertexStride) ||
            !readPOD(stream, mesh.vertexLayoutHash) ||
            !readMaterial(stream, mesh.material) ||
            !readPOD(stream, mesh.localTransform))
            return std::nullopt;
    }

    if (!readSkeleton(stream, modelAsset.skeleton))
        return std::nullopt;

    if (!readAnimations(stream, modelAsset.animations))
        return std::nullopt;

    if (modelAsset.assetPath.empty())
        modelAsset.assetPath = std::filesystem::path(path).lexically_normal().string();

    return modelAsset;
}

ELIX_NESTED_NAMESPACE_END
