#include "Engine/Assets/AssetsSerializer.hpp"
#include "Engine/Assets/Compressor.hpp"

#include "Core/Logger.hpp"

#include <algorithm>
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

    bool writeHeader(std::ostream &stream, elix::engine::Asset::AssetType type, uint64_t payloadSize, uint8_t reserved0 = 0u, uint8_t reserved1 = 0u, uint8_t reserved2 = 0u)
    {
        elix::engine::Asset::BinaryHeader header{};
        std::memcpy(header.magic, elix::engine::Asset::MAGIC.data(), elix::engine::Asset::MAGIC.size());
        header.version = elix::engine::Asset::VERSION;
        header.type = static_cast<uint8_t>(type);
        header.reserved[0] = reserved0;
        header.reserved[1] = reserved1;
        header.reserved[2] = reserved2;
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
        {
            const VkFormat format = static_cast<VkFormat>(textureAsset.vkFormat);

            uint64_t blockBytes = 0u;
            switch (format)
            {
            case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
            case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
            case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            case VK_FORMAT_BC4_UNORM_BLOCK:
            case VK_FORMAT_BC4_SNORM_BLOCK:
                blockBytes = 8u;
                break;
            case VK_FORMAT_BC2_UNORM_BLOCK:
            case VK_FORMAT_BC2_SRGB_BLOCK:
            case VK_FORMAT_BC3_UNORM_BLOCK:
            case VK_FORMAT_BC3_SRGB_BLOCK:
            case VK_FORMAT_BC5_UNORM_BLOCK:
            case VK_FORMAT_BC5_SNORM_BLOCK:
            case VK_FORMAT_BC6H_UFLOAT_BLOCK:
            case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            case VK_FORMAT_BC7_UNORM_BLOCK:
            case VK_FORMAT_BC7_SRGB_BLOCK:
                blockBytes = 16u;
                break;
            default:
                return std::nullopt;
            }

            const uint64_t blocksWide = std::max<uint64_t>(1u, (width + 3u) / 4u);
            const uint64_t blocksHigh = std::max<uint64_t>(1u, (height + 3u) / 4u);
            if (blocksWide > (std::numeric_limits<uint64_t>::max() / blocksHigh))
                return std::nullopt;
            const uint64_t blockCount = blocksWide * blocksHigh;
            if (blockCount > (std::numeric_limits<uint64_t>::max() / blockBytes))
                return std::nullopt;

            return blockCount * blockBytes;
        }
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

    if (!textureAsset.pixels.empty())
    {
        std::vector<uint8_t> compressedPixels;
        if (Compressor::compress(textureAsset.pixels, compressedPixels, Compressor::Algorithm::Deflate, 7))
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

    const uint8_t extraMipCount = static_cast<uint8_t>(std::min(textureAsset.mipChain.size(), static_cast<size_t>(255u)));
    for (uint8_t i = 0u; i < extraMipCount; ++i)
    {
        if (!writeBytes(payloadStream, textureAsset.mipChain[i]))
            return false;
    }

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

    if (!writeHeader(stream, Asset::AssetType::TEXTURE, static_cast<uint64_t>(payload.size()), compressionAlgorithm, extraMipCount))
        return false;

    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return stream.good();
}

bool AssetsSerializer::writeModel(const ModelAsset &modelAsset, const std::string &outputPath) const
{
    constexpr uint8_t kModelPayloadVersionWithBoneAttachments = 2u;

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
            !writePOD(payloadStream, mesh.localTransform) ||
            !writePOD(payloadStream, mesh.attachedBoneId))
            return false;
    }

    if (!writeSkeleton(payloadStream, modelAsset.skeleton))
        return false;

    if (!writeAnimations(payloadStream, modelAsset.animations))
        return false;

    const std::string payload = payloadStream.str();
    const std::vector<uint8_t> payloadBytes(payload.begin(), payload.end());

    std::vector<uint8_t> storedPayload = payloadBytes;
    uint8_t compressionAlgorithm = static_cast<uint8_t>(Compressor::Algorithm::None);

    if (!payloadBytes.empty())
    {
        std::vector<uint8_t> compressedPayload;
        if (Compressor::compress(payloadBytes, compressedPayload, Compressor::Algorithm::Deflate, 7))
        {
            // Avoid paying decompression cost when compression gain is minimal.
            if (compressedPayload.size() + 256u < payloadBytes.size())
            {
                storedPayload = std::move(compressedPayload);
                compressionAlgorithm = static_cast<uint8_t>(Compressor::Algorithm::Deflate);
            }
        }
    }

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

    uint64_t storedPayloadSize = static_cast<uint64_t>(storedPayload.size());
    if (compressionAlgorithm != static_cast<uint8_t>(Compressor::Algorithm::None))
        storedPayloadSize += sizeof(uint64_t);

    if (!writeHeader(stream, Asset::AssetType::MODEL, storedPayloadSize, compressionAlgorithm, kModelPayloadVersionWithBoneAttachments))
        return false;

    if (compressionAlgorithm != static_cast<uint8_t>(Compressor::Algorithm::None))
    {
        const uint64_t uncompressedSize = static_cast<uint64_t>(payloadBytes.size());
        if (!writePOD(stream, uncompressedSize))
            return false;
    }

    if (!storedPayload.empty())
        stream.write(reinterpret_cast<const char *>(storedPayload.data()), static_cast<std::streamsize>(storedPayload.size()));

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

    const uint8_t extraMipCount = header.reserved[1];
    if (extraMipCount > 0u)
    {
        textureAsset.mipChain.resize(extraMipCount);
        for (uint8_t i = 0u; i < extraMipCount; ++i)
        {
            if (!readBytes(stream, textureAsset.mipChain[i]))
            {
                textureAsset.mipChain.resize(i);
                break;
            }
        }
    }

    if (textureAsset.assetPath.empty())
        textureAsset.assetPath = std::filesystem::path(path).lexically_normal().string();

    return textureAsset;
}

std::optional<TextureAsset> AssetsSerializer::readTexture(const std::vector<uint8_t> &bytes) const
{
    std::string str(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    std::istringstream stream(std::move(str), std::ios::binary);
    // Reuse path-based implementation via a temporary file path for error messages.
    // The actual parse is identical — just redirect through istringstream.
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
            return std::nullopt;

        std::vector<uint8_t> decompressedPixels;
        if (!Compressor::decompress(textureAsset.pixels, static_cast<size_t>(expectedSize.value()), decompressedPixels, compressionAlgorithm))
            return std::nullopt;

        textureAsset.pixels = std::move(decompressedPixels);
    }

    const uint8_t extraMipCount = header.reserved[1];
    if (extraMipCount > 0u)
    {
        textureAsset.mipChain.resize(extraMipCount);
        for (uint8_t i = 0u; i < extraMipCount; ++i)
        {
            if (!readBytes(stream, textureAsset.mipChain[i]))
            {
                textureAsset.mipChain.resize(i);
                break;
            }
        }
    }

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

    constexpr uint8_t kLegacyModelPayloadVersion = 1u;
    const uint8_t modelPayloadVersion = header.reserved[1] == 0u ? kLegacyModelPayloadVersion : header.reserved[1];

    auto parseModelPayload = [](std::istream &payloadStream, const std::string &assetPath, uint8_t modelPayloadVersion) -> std::optional<ModelAsset>
    {
        constexpr uint8_t kModelPayloadVersionWithBoneAttachments = 2u;

        ModelAsset modelAsset{{}, std::nullopt, {}};
        if (!readString(payloadStream, modelAsset.sourcePath) ||
            !readString(payloadStream, modelAsset.assetPath))
            return std::nullopt;

        uint32_t meshesCount = 0u;
        if (!readPOD(payloadStream, meshesCount))
            return std::nullopt;

        if (meshesCount > (1u << 16))
            return std::nullopt;

        modelAsset.meshes.resize(meshesCount);
        for (uint32_t meshIndex = 0; meshIndex < meshesCount; ++meshIndex)
        {
            auto &mesh = modelAsset.meshes[meshIndex];
            if (!readString(payloadStream, mesh.name) ||
                !readVector(payloadStream, mesh.vertexData, 1ull << 31) ||
                !readVector(payloadStream, mesh.indices, 1ull << 31) ||
                !readPOD(payloadStream, mesh.vertexStride) ||
                !readPOD(payloadStream, mesh.vertexLayoutHash) ||
                !readMaterial(payloadStream, mesh.material) ||
                !readPOD(payloadStream, mesh.localTransform))
                return std::nullopt;

            mesh.attachedBoneId = -1;
            if (modelPayloadVersion >= kModelPayloadVersionWithBoneAttachments &&
                !readPOD(payloadStream, mesh.attachedBoneId))
                return std::nullopt;
        }

        if (!readSkeleton(payloadStream, modelAsset.skeleton))
            return std::nullopt;

        if (!readAnimations(payloadStream, modelAsset.animations))
            return std::nullopt;

        if (modelAsset.assetPath.empty())
            modelAsset.assetPath = std::filesystem::path(assetPath).lexically_normal().string();

        return modelAsset;
    };

    const auto compressionAlgorithm = static_cast<Compressor::Algorithm>(header.reserved[0]);
    if (compressionAlgorithm == Compressor::Algorithm::None)
        return parseModelPayload(stream, path, modelPayloadVersion);

    if (compressionAlgorithm != Compressor::Algorithm::Deflate)
    {
        VX_ENGINE_ERROR_STREAM("Unsupported model payload compression algorithm in asset: " << path << '\n');
        return std::nullopt;
    }

    uint64_t uncompressedSize = 0u;
    if (!readPOD(stream, uncompressedSize))
        return std::nullopt;

    if (uncompressedSize == 0u || uncompressedSize > (1ull << 33))
    {
        VX_ENGINE_ERROR_STREAM("Invalid compressed model payload size in asset: " << path << '\n');
        return std::nullopt;
    }

    const uint64_t payloadBytesInHeader = header.payloadSize;
    if (payloadBytesInHeader < sizeof(uint64_t))
        return std::nullopt;

    const uint64_t compressedSize = payloadBytesInHeader - sizeof(uint64_t);
    if (compressedSize > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return std::nullopt;

    std::vector<uint8_t> compressedPayload(static_cast<size_t>(compressedSize));
    if (compressedSize > 0u)
    {
        stream.read(reinterpret_cast<char *>(compressedPayload.data()), static_cast<std::streamsize>(compressedPayload.size()));
        if (!stream.good())
            return std::nullopt;
    }

    std::vector<uint8_t> decompressedPayload;
    if (!Compressor::decompress(compressedPayload, static_cast<size_t>(uncompressedSize), decompressedPayload, compressionAlgorithm))
    {
        VX_ENGINE_ERROR_STREAM("Failed to decompress model payload: " << path << '\n');
        return std::nullopt;
    }

    std::istringstream payloadStream(
        std::string(reinterpret_cast<const char *>(decompressedPayload.data()), decompressedPayload.size()),
        std::ios::binary);

    return parseModelPayload(payloadStream, path, modelPayloadVersion);
}

std::optional<ModelAsset> AssetsSerializer::readModel(const std::vector<uint8_t> &bytes) const
{
    std::string str(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    std::istringstream outerStream(std::move(str), std::ios::binary);

    Asset::BinaryHeader header{};
    if (!::readHeader(outerStream, header))
        return std::nullopt;

    if (static_cast<Asset::AssetType>(header.type) != Asset::AssetType::MODEL)
        return std::nullopt;

    constexpr uint8_t kLegacyModelPayloadVersion = 1u;
    const uint8_t modelPayloadVersion = header.reserved[1] == 0u ? kLegacyModelPayloadVersion : header.reserved[1];

    auto parseModelPayload = [](std::istream &payloadStream, const std::string &assetPath, uint8_t modelPayloadVersion) -> std::optional<ModelAsset>
    {
        constexpr uint8_t kModelPayloadVersionWithBoneAttachments = 2u;

        ModelAsset modelAsset{{}, std::nullopt, {}};
        if (!readString(payloadStream, modelAsset.sourcePath) ||
            !readString(payloadStream, modelAsset.assetPath))
            return std::nullopt;

        uint32_t meshesCount = 0u;
        if (!readPOD(payloadStream, meshesCount))
            return std::nullopt;

        if (meshesCount > (1u << 16))
            return std::nullopt;

        modelAsset.meshes.resize(meshesCount);
        for (uint32_t meshIndex = 0; meshIndex < meshesCount; ++meshIndex)
        {
            auto &mesh = modelAsset.meshes[meshIndex];
            if (!readString(payloadStream, mesh.name) ||
                !readVector(payloadStream, mesh.vertexData, 1ull << 31) ||
                !readVector(payloadStream, mesh.indices, 1ull << 31) ||
                !readPOD(payloadStream, mesh.vertexStride) ||
                !readPOD(payloadStream, mesh.vertexLayoutHash) ||
                !readMaterial(payloadStream, mesh.material) ||
                !readPOD(payloadStream, mesh.localTransform))
                return std::nullopt;

            mesh.attachedBoneId = -1;
            if (modelPayloadVersion >= kModelPayloadVersionWithBoneAttachments &&
                !readPOD(payloadStream, mesh.attachedBoneId))
                return std::nullopt;
        }

        if (!readSkeleton(payloadStream, modelAsset.skeleton))
            return std::nullopt;

        if (!readAnimations(payloadStream, modelAsset.animations))
            return std::nullopt;

        if (modelAsset.assetPath.empty())
            modelAsset.assetPath = std::filesystem::path(assetPath).lexically_normal().string();

        return modelAsset;
    };

    const auto compressionAlgorithm = static_cast<Compressor::Algorithm>(header.reserved[0]);
    if (compressionAlgorithm == Compressor::Algorithm::None)
        return parseModelPayload(outerStream, {}, modelPayloadVersion);

    uint64_t uncompressedSize = 0u;
    if (!readPOD(outerStream, uncompressedSize) || uncompressedSize == 0u || uncompressedSize > (1ull << 33))
        return std::nullopt;

    const uint64_t payloadBytesInHeader = header.payloadSize;
    if (payloadBytesInHeader < sizeof(uint64_t))
        return std::nullopt;

    const uint64_t compressedSize = payloadBytesInHeader - sizeof(uint64_t);
    std::vector<uint8_t> compressedPayload(static_cast<size_t>(compressedSize));
    if (compressedSize > 0u)
    {
        outerStream.read(reinterpret_cast<char *>(compressedPayload.data()), static_cast<std::streamsize>(compressedPayload.size()));
        if (!outerStream.good())
            return std::nullopt;
    }

    std::vector<uint8_t> decompressedPayload;
    if (!Compressor::decompress(compressedPayload, static_cast<size_t>(uncompressedSize), decompressedPayload, compressionAlgorithm))
        return std::nullopt;

    std::istringstream payloadStream(
        std::string(reinterpret_cast<const char *>(decompressedPayload.data()), decompressedPayload.size()),
        std::ios::binary);

    return parseModelPayload(payloadStream, {}, modelPayloadVersion);
}

bool AssetsSerializer::writeAudio(const AudioAsset &audioAsset, const std::string &outputPath) const
{
    std::ostringstream payloadStream(std::ios::binary);
    if (!writeString(payloadStream, audioAsset.name) ||
        !writeString(payloadStream, audioAsset.sourcePath) ||
        !writeString(payloadStream, audioAsset.assetPath) ||
        !writeBytes(payloadStream, audioAsset.audioData))
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
        VX_ENGINE_ERROR_STREAM("Failed to open audio asset output file: " << outputPath << '\n');
        return false;
    }

    if (!writeHeader(stream, Asset::AssetType::AUDIO, static_cast<uint64_t>(payload.size())))
        return false;

    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return stream.good();
}

std::optional<AudioAsset> AssetsSerializer::readAudio(const std::string &path) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return std::nullopt;

    Asset::BinaryHeader header{};
    if (!::readHeader(stream, header))
        return std::nullopt;

    if (static_cast<Asset::AssetType>(header.type) != Asset::AssetType::AUDIO)
        return std::nullopt;

    AudioAsset audioAsset{};
    if (!readString(stream, audioAsset.name) ||
        !readString(stream, audioAsset.sourcePath) ||
        !readString(stream, audioAsset.assetPath) ||
        !readBytes(stream, audioAsset.audioData))
        return std::nullopt;

    if (audioAsset.assetPath.empty())
        audioAsset.assetPath = std::filesystem::path(path).lexically_normal().string();

    return audioAsset;
}

bool AssetsSerializer::writeAnimationAsset(const AnimationAsset &animationAsset, const std::string &outputPath) const
{
    std::ostringstream payloadStream(std::ios::binary);
    if (!writeString(payloadStream, animationAsset.name) ||
        !writeString(payloadStream, animationAsset.sourcePath) ||
        !writeString(payloadStream, animationAsset.assetPath) ||
        !writeAnimations(payloadStream, animationAsset.animations))
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
        VX_ENGINE_ERROR_STREAM("Failed to open animation asset output file: " << outputPath << '\n');
        return false;
    }

    if (!writeHeader(stream, Asset::AssetType::ANIMATION, static_cast<uint64_t>(payload.size())))
        return false;

    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return stream.good();
}

std::optional<AnimationAsset> AssetsSerializer::readAnimationAsset(const std::string &path) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return std::nullopt;

    Asset::BinaryHeader header{};
    if (!::readHeader(stream, header))
        return std::nullopt;

    if (static_cast<Asset::AssetType>(header.type) != Asset::AssetType::ANIMATION)
        return std::nullopt;

    AnimationAsset animationAsset{};
    if (!readString(stream, animationAsset.name) ||
        !readString(stream, animationAsset.sourcePath) ||
        !readString(stream, animationAsset.assetPath) ||
        !readAnimations(stream, animationAsset.animations))
        return std::nullopt;

    if (animationAsset.assetPath.empty())
        animationAsset.assetPath = std::filesystem::path(path).lexically_normal().string();

    return animationAsset;
}

bool AssetsSerializer::writeAnimationTree(const AnimationTree &tree, const std::string &outputPath) const
{
    std::ostringstream payloadStream(std::ios::binary);

    if (!writeString(payloadStream, tree.name) ||
        !writeString(payloadStream, tree.assetPath))
        return false;

    if (!writePOD(payloadStream, static_cast<int32_t>(tree.entryStateIndex)))
        return false;

    const uint32_t paramCount = static_cast<uint32_t>(tree.parameters.size());
    if (!writePOD(payloadStream, paramCount))
        return false;
    for (const auto &param : tree.parameters)
    {
        if (!writeString(payloadStream, param.name))
            return false;
        if (!writePOD(payloadStream, static_cast<uint8_t>(param.type)))
            return false;
        if (!writePOD(payloadStream, param.floatDefault))
            return false;
        if (!writePOD(payloadStream, static_cast<uint8_t>(param.boolDefault ? 1 : 0)))
            return false;
        if (!writePOD(payloadStream, static_cast<int32_t>(param.intDefault)))
            return false;
    }

    const uint32_t stateCount = static_cast<uint32_t>(tree.states.size());
    if (!writePOD(payloadStream, stateCount))
        return false;
    for (const auto &state : tree.states)
    {
        if (!writeString(payloadStream, state.name) ||
            !writeString(payloadStream, state.animationAssetPath))
            return false;
        if (!writePOD(payloadStream, static_cast<int32_t>(state.clipIndex)))
            return false;
        if (!writePOD(payloadStream, static_cast<uint8_t>(state.loop ? 1 : 0)))
            return false;
        if (!writePOD(payloadStream, state.speed))
            return false;
    }

    const uint32_t transitionCount = static_cast<uint32_t>(tree.transitions.size());
    if (!writePOD(payloadStream, transitionCount))
        return false;
    for (const auto &transition : tree.transitions)
    {
        if (!writePOD(payloadStream, static_cast<int32_t>(transition.fromStateIndex)) ||
            !writePOD(payloadStream, static_cast<int32_t>(transition.toStateIndex)) ||
            !writePOD(payloadStream, transition.blendDuration) ||
            !writePOD(payloadStream, static_cast<uint8_t>(transition.hasExitTime ? 1 : 0)) ||
            !writePOD(payloadStream, transition.exitTime))
            return false;

        const uint32_t conditionCount = static_cast<uint32_t>(transition.conditions.size());
        if (!writePOD(payloadStream, conditionCount))
            return false;
        for (const auto &cond : transition.conditions)
        {
            if (!writePOD(payloadStream, static_cast<uint8_t>(cond.type)))
                return false;
            if (!writeString(payloadStream, cond.parameterName))
                return false;
            if (!writePOD(payloadStream, cond.floatThreshold))
                return false;
            if (!writePOD(payloadStream, static_cast<int32_t>(cond.intValue)))
                return false;
        }
    }

    const uint32_t posCount = static_cast<uint32_t>(tree.stateNodePositions.size());
    if (!writePOD(payloadStream, posCount))
        return false;
    for (const auto &pos : tree.stateNodePositions)
    {
        if (!writePOD(payloadStream, pos.x) || !writePOD(payloadStream, pos.y))
            return false;
    }

    const std::string payload = payloadStream.str();

    std::error_code directoryError;
    const auto outputFilesystemPath = std::filesystem::path(outputPath).lexically_normal();
    const auto parentPath = outputFilesystemPath.parent_path();
    if (!parentPath.empty())
        std::filesystem::create_directories(parentPath, directoryError);

    std::ofstream stream(outputFilesystemPath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        VX_ENGINE_ERROR_STREAM("Failed to open animation tree output file: " << outputPath << '\n');
        return false;
    }

    if (!writeHeader(stream, Asset::AssetType::ANIMATION_TREE, static_cast<uint64_t>(payload.size())))
        return false;

    stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    return stream.good();
}

std::optional<AnimationTree> AssetsSerializer::readAnimationTree(const std::string &path) const
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return std::nullopt;

    Asset::BinaryHeader header{};
    if (!::readHeader(stream, header))
        return std::nullopt;

    if (static_cast<Asset::AssetType>(header.type) != Asset::AssetType::ANIMATION_TREE)
        return std::nullopt;

    AnimationTree tree{};

    if (!readString(stream, tree.name) ||
        !readString(stream, tree.assetPath))
        return std::nullopt;

    int32_t entryIndex = 0;
    if (!readPOD(stream, entryIndex))
        return std::nullopt;
    tree.entryStateIndex = static_cast<int>(entryIndex);

    uint32_t paramCount = 0;
    if (!readPOD(stream, paramCount) || paramCount > 1024u)
        return std::nullopt;
    tree.parameters.resize(paramCount);
    for (auto &param : tree.parameters)
    {
        uint8_t typeVal = 0;
        uint8_t boolVal = 0;
        int32_t intVal = 0;
        if (!readString(stream, param.name) ||
            !readPOD(stream, typeVal) ||
            !readPOD(stream, param.floatDefault) ||
            !readPOD(stream, boolVal) ||
            !readPOD(stream, intVal))
            return std::nullopt;
        param.type = static_cast<AnimationTreeParameter::Type>(typeVal);
        param.boolDefault = (boolVal != 0);
        param.intDefault = static_cast<int>(intVal);
    }

    uint32_t stateCount = 0;
    if (!readPOD(stream, stateCount) || stateCount > 4096u)
        return std::nullopt;
    tree.states.resize(stateCount);
    for (auto &state : tree.states)
    {
        uint8_t loopVal = 0;
        int32_t clipIdx = 0;
        if (!readString(stream, state.name) ||
            !readString(stream, state.animationAssetPath) ||
            !readPOD(stream, clipIdx) ||
            !readPOD(stream, loopVal) ||
            !readPOD(stream, state.speed))
            return std::nullopt;
        state.clipIndex = static_cast<int>(clipIdx);
        state.loop = (loopVal != 0);
    }

    uint32_t transitionCount = 0;
    if (!readPOD(stream, transitionCount) || transitionCount > 65536u)
        return std::nullopt;
    tree.transitions.resize(transitionCount);
    for (auto &transition : tree.transitions)
    {
        int32_t fromIdx = 0, toIdx = 0;
        uint8_t exitTimeFlag = 0;
        if (!readPOD(stream, fromIdx) ||
            !readPOD(stream, toIdx) ||
            !readPOD(stream, transition.blendDuration) ||
            !readPOD(stream, exitTimeFlag) ||
            !readPOD(stream, transition.exitTime))
            return std::nullopt;
        transition.fromStateIndex = static_cast<int>(fromIdx);
        transition.toStateIndex = static_cast<int>(toIdx);
        transition.hasExitTime = (exitTimeFlag != 0);

        uint32_t conditionCount = 0;
        if (!readPOD(stream, conditionCount) || conditionCount > 256u)
            return std::nullopt;
        transition.conditions.resize(conditionCount);
        for (auto &cond : transition.conditions)
        {
            uint8_t typeVal = 0;
            int32_t intVal = 0;
            if (!readPOD(stream, typeVal) ||
                !readString(stream, cond.parameterName) ||
                !readPOD(stream, cond.floatThreshold) ||
                !readPOD(stream, intVal))
                return std::nullopt;
            cond.type = static_cast<AnimationTransitionCondition::Type>(typeVal);
            cond.intValue = static_cast<int>(intVal);
        }
    }

    uint32_t posCount = 0;
    if (!readPOD(stream, posCount) || posCount > 4096u)
        return std::nullopt;
    tree.stateNodePositions.resize(posCount);
    for (auto &pos : tree.stateNodePositions)
    {
        if (!readPOD(stream, pos.x) || !readPOD(stream, pos.y))
            return std::nullopt;
    }

    if (tree.assetPath.empty())
        tree.assetPath = std::filesystem::path(path).lexically_normal().string();

    return tree;
}

ELIX_NESTED_NAMESPACE_END
