#include "Engine/Assets/AssetsLoader.hpp"

#include "Engine/Assets/AssetsSerializer.hpp"

#include "Core/Logger.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <typeinfo>
#include <unordered_map>

#if defined(ELIX_HAS_OPENEXR)
#include <ImathBox.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfInputFile.h>
#endif

namespace
{
    constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "
    constexpr uint32_t DDPF_FOURCC = 0x00000004u;
    constexpr uint32_t DDPF_RGB = 0x00000040u;

    constexpr uint32_t makeFourCC(char c0, char c1, char c2, char c3)
    {
        return static_cast<uint32_t>(static_cast<uint8_t>(c0)) |
               (static_cast<uint32_t>(static_cast<uint8_t>(c1)) << 8u) |
               (static_cast<uint32_t>(static_cast<uint8_t>(c2)) << 16u) |
               (static_cast<uint32_t>(static_cast<uint8_t>(c3)) << 24u);
    }

#pragma pack(push, 1)
    struct DDSPixelFormat
    {
        uint32_t size;
        uint32_t flags;
        uint32_t fourCC;
        uint32_t rgbBitCount;
        uint32_t rBitMask;
        uint32_t gBitMask;
        uint32_t bBitMask;
        uint32_t aBitMask;
    };

    struct DDSHeader
    {
        uint32_t size;
        uint32_t flags;
        uint32_t height;
        uint32_t width;
        uint32_t pitchOrLinearSize;
        uint32_t depth;
        uint32_t mipMapCount;
        uint32_t reserved1[11];
        DDSPixelFormat pixelFormat;
        uint32_t caps;
        uint32_t caps2;
        uint32_t caps3;
        uint32_t caps4;
        uint32_t reserved2;
    };

    struct DDSHeaderDX10
    {
        uint32_t dxgiFormat;
        uint32_t resourceDimension;
        uint32_t miscFlag;
        uint32_t arraySize;
        uint32_t miscFlags2;
    };
#pragma pack(pop)

    enum class DDSPixelPacking
    {
        None = 0,
        RGBA8,
        BGRA8
    };

    struct DDSLoadResult
    {
        uint32_t width{0};
        uint32_t height{0};
        VkFormat format{VK_FORMAT_UNDEFINED};
        DDSPixelPacking packing{DDSPixelPacking::None};
        bool compressed{false};
        std::vector<uint8_t> topLevelBytes;
    };

    std::string toLowerCopy(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return text;
    }

    std::string extensionLower(const std::string &path)
    {
        return toLowerCopy(std::filesystem::path(path).extension().string());
    }

    bool looksLikeWindowsAbsolutePath(const std::string &path)
    {
        return path.size() >= 3u &&
               std::isalpha(static_cast<unsigned char>(path[0])) &&
               path[1] == ':' &&
               (path[2] == '\\' || path[2] == '/');
    }

    std::filesystem::path normalizePath(const std::string &path)
    {
        if (path.empty())
            return {};

        if (looksLikeWindowsAbsolutePath(path))
        {
            std::string portablePath = path;
            std::replace(portablePath.begin(), portablePath.end(), '\\', '/');
            return std::filesystem::path(portablePath).lexically_normal();
        }

        std::error_code errorCode;
        std::filesystem::path filesystemPath = std::filesystem::path(path);
        const std::filesystem::path absolutePath = std::filesystem::absolute(filesystemPath, errorCode);
        if (!errorCode)
            filesystemPath = absolutePath;

        return filesystemPath.lexically_normal();
    }

    bool isElixAssetFile(const std::filesystem::path &path)
    {
        return toLowerCopy(path.extension().string()) == ".elixasset";
    }

    bool prefersSrgb(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
        }
    }

    VkFormat resolveColorFormat(VkFormat requested, VkFormat unormFormat, VkFormat srgbFormat)
    {
        if (prefersSrgb(requested) && srgbFormat != VK_FORMAT_UNDEFINED)
            return srgbFormat;

        return unormFormat;
    }

    bool isCompressedDDSFormat(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
        }
    }

    uint32_t compressedBlockByteSize(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 8;
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
            return 16;
        default:
            return 0;
        }
    }

    bool mapLegacyDDSFormat(uint32_t fourCC, VkFormat requestedFormat, VkFormat &outFormat)
    {
        switch (fourCC)
        {
        case makeFourCC('D', 'X', 'T', '1'):
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
            return true;
        case makeFourCC('D', 'X', 'T', '3'):
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC2_SRGB_BLOCK);
            return true;
        case makeFourCC('D', 'X', 'T', '5'):
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK);
            return true;
        case makeFourCC('A', 'T', 'I', '1'):
        case makeFourCC('B', 'C', '4', 'U'):
            outFormat = VK_FORMAT_BC4_UNORM_BLOCK;
            return true;
        case makeFourCC('B', 'C', '4', 'S'):
            outFormat = VK_FORMAT_BC4_SNORM_BLOCK;
            return true;
        case makeFourCC('A', 'T', 'I', '2'):
        case makeFourCC('B', 'C', '5', 'U'):
            outFormat = VK_FORMAT_BC5_UNORM_BLOCK;
            return true;
        case makeFourCC('B', 'C', '5', 'S'):
            outFormat = VK_FORMAT_BC5_SNORM_BLOCK;
            return true;
        default:
            return false;
        }
    }

    bool mapDxgiToVkFormat(uint32_t dxgiFormat, VkFormat requestedFormat, VkFormat &outFormat)
    {
        switch (dxgiFormat)
        {
        case 28: // DXGI_FORMAT_R8G8B8A8_UNORM
        case 29: // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB);
            return true;
        case 87: // DXGI_FORMAT_B8G8R8A8_UNORM
        case 91: // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB);
            return true;
        case 71: // DXGI_FORMAT_BC1_UNORM
        case 72: // DXGI_FORMAT_BC1_UNORM_SRGB
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
            return true;
        case 74: // DXGI_FORMAT_BC2_UNORM
        case 75: // DXGI_FORMAT_BC2_UNORM_SRGB
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_BC2_UNORM_BLOCK, VK_FORMAT_BC2_SRGB_BLOCK);
            return true;
        case 77: // DXGI_FORMAT_BC3_UNORM
        case 78: // DXGI_FORMAT_BC3_UNORM_SRGB
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_BC3_UNORM_BLOCK, VK_FORMAT_BC3_SRGB_BLOCK);
            return true;
        case 80: // DXGI_FORMAT_BC4_UNORM
            outFormat = VK_FORMAT_BC4_UNORM_BLOCK;
            return true;
        case 81: // DXGI_FORMAT_BC4_SNORM
            outFormat = VK_FORMAT_BC4_SNORM_BLOCK;
            return true;
        case 83: // DXGI_FORMAT_BC5_UNORM
            outFormat = VK_FORMAT_BC5_UNORM_BLOCK;
            return true;
        case 84: // DXGI_FORMAT_BC5_SNORM
            outFormat = VK_FORMAT_BC5_SNORM_BLOCK;
            return true;
        case 95: // DXGI_FORMAT_BC6H_UF16
            outFormat = VK_FORMAT_BC6H_UFLOAT_BLOCK;
            return true;
        case 96: // DXGI_FORMAT_BC6H_SF16
            outFormat = VK_FORMAT_BC6H_SFLOAT_BLOCK;
            return true;
        case 98: // DXGI_FORMAT_BC7_UNORM
        case 99: // DXGI_FORMAT_BC7_UNORM_SRGB
            outFormat = resolveColorFormat(requestedFormat, VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK);
            return true;
        default:
            return false;
        }
    }

    bool loadDDS(const std::string &path, VkFormat requestedFormat, DDSLoadResult &outResult)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            VX_ENGINE_ERROR_STREAM("Failed to open DDS file: " << path << '\n');
            return false;
        }

        const std::streamsize fileSize = file.tellg();
        if (fileSize <= 0 || static_cast<size_t>(fileSize) < sizeof(uint32_t) + sizeof(DDSHeader))
        {
            VX_ENGINE_ERROR_STREAM("DDS file is too small or invalid: " << path << '\n');
            return false;
        }

        std::vector<uint8_t> fileBytes(static_cast<size_t>(fileSize));
        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char *>(fileBytes.data()), fileSize))
        {
            VX_ENGINE_ERROR_STREAM("Failed to read DDS file bytes: " << path << '\n');
            return false;
        }

        size_t offset = 0;

        uint32_t magic = 0;
        std::memcpy(&magic, fileBytes.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (magic != DDS_MAGIC)
        {
            VX_ENGINE_ERROR_STREAM("Invalid DDS magic for file: " << path << '\n');
            return false;
        }

        DDSHeader header{};
        std::memcpy(&header, fileBytes.data() + offset, sizeof(DDSHeader));
        offset += sizeof(DDSHeader);

        if (header.size != 124u || header.pixelFormat.size != 32u)
        {
            VX_ENGINE_ERROR_STREAM("Unsupported DDS header layout: " << path << '\n');
            return false;
        }

        if (header.width == 0u || header.height == 0u)
        {
            VX_ENGINE_ERROR_STREAM("DDS has invalid dimensions: " << path << '\n');
            return false;
        }

        VkFormat parsedFormat = VK_FORMAT_UNDEFINED;
        DDSPixelPacking pixelPacking = DDSPixelPacking::None;

        if ((header.pixelFormat.flags & DDPF_FOURCC) != 0u)
        {
            if (header.pixelFormat.fourCC == makeFourCC('D', 'X', '1', '0'))
            {
                if (fileBytes.size() < offset + sizeof(DDSHeaderDX10))
                {
                    VX_ENGINE_ERROR_STREAM("DDS DX10 header missing: " << path << '\n');
                    return false;
                }

                DDSHeaderDX10 headerDX10{};
                std::memcpy(&headerDX10, fileBytes.data() + offset, sizeof(DDSHeaderDX10));
                offset += sizeof(DDSHeaderDX10);

                if (!mapDxgiToVkFormat(headerDX10.dxgiFormat, requestedFormat, parsedFormat))
                {
                    VX_ENGINE_ERROR_STREAM("Unsupported DDS DX10 format (" << headerDX10.dxgiFormat << ") for file: " << path << '\n');
                    return false;
                }
            }
            else if (!mapLegacyDDSFormat(header.pixelFormat.fourCC, requestedFormat, parsedFormat))
            {
                VX_ENGINE_ERROR_STREAM("Unsupported legacy DDS FourCC format for file: " << path << '\n');
                return false;
            }
        }
        else if ((header.pixelFormat.flags & DDPF_RGB) != 0u && header.pixelFormat.rgbBitCount == 32u)
        {
            const bool isRGBA =
                header.pixelFormat.rBitMask == 0x000000ffu &&
                header.pixelFormat.gBitMask == 0x0000ff00u &&
                header.pixelFormat.bBitMask == 0x00ff0000u &&
                (header.pixelFormat.aBitMask == 0xff000000u || header.pixelFormat.aBitMask == 0u);

            const bool isBGRA =
                header.pixelFormat.rBitMask == 0x00ff0000u &&
                header.pixelFormat.gBitMask == 0x0000ff00u &&
                header.pixelFormat.bBitMask == 0x000000ffu &&
                (header.pixelFormat.aBitMask == 0xff000000u || header.pixelFormat.aBitMask == 0u);

            if (!isRGBA && !isBGRA)
            {
                VX_ENGINE_ERROR_STREAM("Unsupported DDS uncompressed channel masks for file: " << path << '\n');
                return false;
            }

            parsedFormat = resolveColorFormat(requestedFormat, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB);
            pixelPacking = isBGRA ? DDSPixelPacking::BGRA8 : DDSPixelPacking::RGBA8;
        }
        else
        {
            VX_ENGINE_ERROR_STREAM("DDS format is not supported by loader: " << path << '\n');
            return false;
        }

        const bool compressed = isCompressedDDSFormat(parsedFormat);
        size_t topLevelSize = 0u;

        if (compressed)
        {
            const uint32_t blockBytes = compressedBlockByteSize(parsedFormat);
            if (blockBytes == 0u)
            {
                VX_ENGINE_ERROR_STREAM("Unsupported compressed DDS block format for file: " << path << '\n');
                return false;
            }

            const uint32_t blocksWide = std::max(1u, (header.width + 3u) / 4u);
            const uint32_t blocksHigh = std::max(1u, (header.height + 3u) / 4u);
            topLevelSize = static_cast<size_t>(blocksWide) * static_cast<size_t>(blocksHigh) * static_cast<size_t>(blockBytes);
        }
        else
            topLevelSize = static_cast<size_t>(header.width) * static_cast<size_t>(header.height) * 4u;

        if (fileBytes.size() < offset + topLevelSize)
        {
            VX_ENGINE_ERROR_STREAM("DDS pixel payload is truncated: " << path << '\n');
            return false;
        }

        outResult.width = header.width;
        outResult.height = header.height;
        outResult.format = parsedFormat;
        outResult.packing = pixelPacking;
        outResult.compressed = compressed;
        outResult.topLevelBytes.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                       fileBytes.begin() + static_cast<std::ptrdiff_t>(offset + topLevelSize));

        if (!compressed && outResult.packing == DDSPixelPacking::BGRA8)
        {
            for (size_t byteOffset = 0; byteOffset + 3u < outResult.topLevelBytes.size(); byteOffset += 4u)
                std::swap(outResult.topLevelBytes[byteOffset + 0u], outResult.topLevelBytes[byteOffset + 2u]);
        }

        return true;
    }

    bool loadFloatPixelsSTB(const std::string &path, std::vector<float> &outPixels, int &outWidth, int &outHeight)
    {
        int channels = 0;
        float *data = stbi_loadf(path.c_str(), &outWidth, &outHeight, &channels, STBI_rgb_alpha);

        if (!data)
            return false;

        outPixels.assign(data, data + static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4u);
        stbi_image_free(data);
        return true;
    }

#if defined(ELIX_HAS_OPENEXR)
    bool loadFloatPixelsEXR(const std::string &path, std::vector<float> &outPixels, int &outWidth, int &outHeight)
    {
        try
        {
            OPENEXR_IMF_NAMESPACE::InputFile file(path.c_str());
            const OPENEXR_IMF_NAMESPACE::Header &header = file.header();
            const IMATH_NAMESPACE::Box2i dataWindow = header.dataWindow();
            outWidth = dataWindow.max.x - dataWindow.min.x + 1;
            outHeight = dataWindow.max.y - dataWindow.min.y + 1;

            const size_t pixelCount = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);
            std::vector<float> red(pixelCount, 0.0f);
            std::vector<float> green(pixelCount, 0.0f);
            std::vector<float> blue(pixelCount, 0.0f);
            std::vector<float> alpha(pixelCount, 1.0f);

            auto makeBasePointer = [&](std::vector<float> &channel) -> char *
            {
                return reinterpret_cast<char *>(channel.data() - dataWindow.min.x - dataWindow.min.y * outWidth);
            };

            OPENEXR_IMF_NAMESPACE::FrameBuffer frameBuffer;
            const OPENEXR_IMF_NAMESPACE::ChannelList &channels = header.channels();

            if (channels.findChannel("R"))
                frameBuffer.insert("R", OPENEXR_IMF_NAMESPACE::Slice(OPENEXR_IMF_NAMESPACE::FLOAT, makeBasePointer(red), sizeof(float), sizeof(float) * outWidth));
            if (channels.findChannel("G"))
                frameBuffer.insert("G", OPENEXR_IMF_NAMESPACE::Slice(OPENEXR_IMF_NAMESPACE::FLOAT, makeBasePointer(green), sizeof(float), sizeof(float) * outWidth));
            if (channels.findChannel("B"))
                frameBuffer.insert("B", OPENEXR_IMF_NAMESPACE::Slice(OPENEXR_IMF_NAMESPACE::FLOAT, makeBasePointer(blue), sizeof(float), sizeof(float) * outWidth));
            if (channels.findChannel("A"))
                frameBuffer.insert("A", OPENEXR_IMF_NAMESPACE::Slice(OPENEXR_IMF_NAMESPACE::FLOAT, makeBasePointer(alpha), sizeof(float), sizeof(float) * outWidth));

            file.setFrameBuffer(frameBuffer);
            file.readPixels(dataWindow.min.y, dataWindow.max.y);

            if (!channels.findChannel("R") && !channels.findChannel("G") && !channels.findChannel("B"))
            {
                std::vector<float> luminance(pixelCount, 0.0f);
                frameBuffer.insert("Y", OPENEXR_IMF_NAMESPACE::Slice(OPENEXR_IMF_NAMESPACE::FLOAT, makeBasePointer(luminance), sizeof(float), sizeof(float) * outWidth));
                file.setFrameBuffer(frameBuffer);
                file.readPixels(dataWindow.min.y, dataWindow.max.y);
                red = luminance;
                green = luminance;
                blue = luminance;
            }

            outPixels.resize(pixelCount * 4u);
            for (size_t index = 0; index < pixelCount; ++index)
            {
                outPixels[index * 4u + 0u] = red[index];
                outPixels[index * 4u + 1u] = green[index];
                outPixels[index * 4u + 2u] = blue[index];
                outPixels[index * 4u + 3u] = alpha[index];
            }

            return true;
        }
        catch (const std::exception &exception)
        {
            VX_ENGINE_ERROR_STREAM("Failed to decode EXR image: " << path << " (" << exception.what() << ")\n");
            return false;
        }
    }
#endif
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void AssetsLoader::registerAssetLoader(const std::shared_ptr<IAssetLoader> &assetLoader)
{
    if (!assetLoader)
        return;

    for (const auto &registeredLoader : s_assetLoaders)
    {
        if (!registeredLoader)
            continue;

        if (typeid(*registeredLoader) == typeid(*assetLoader))
            return;
    }

    s_assetLoaders.push_back(assetLoader);
}

void AssetsLoader::clearAssetLoaders()
{
    s_assetLoaders.clear();
}

std::filesystem::path AssetsLoader::toModelAssetPath(const std::filesystem::path &sourcePath)
{
    if (isElixAssetFile(sourcePath))
        return sourcePath;

    auto outputPath = sourcePath;
    outputPath.replace_extension(".model.elixasset");
    return outputPath.lexically_normal();
}

std::filesystem::path AssetsLoader::toTextureAssetPath(const std::filesystem::path &sourcePath)
{
    if (isElixAssetFile(sourcePath))
        return sourcePath;

    auto outputPath = sourcePath;
    outputPath.replace_extension(".tex.elixasset");
    return outputPath.lexically_normal();
}

bool AssetsLoader::needsReimport(const std::filesystem::path &sourcePath, const std::filesystem::path &serializedPath)
{
    std::error_code errorCode;
    if (!std::filesystem::exists(serializedPath, errorCode) || errorCode)
        return true;

    errorCode.clear();
    if (!std::filesystem::exists(sourcePath, errorCode) || errorCode)
        return false;

    errorCode.clear();
    const auto sourceWriteTime = std::filesystem::last_write_time(sourcePath, errorCode);
    if (errorCode)
        return false;

    errorCode.clear();
    const auto serializedWriteTime = std::filesystem::last_write_time(serializedPath, errorCode);
    if (errorCode)
        return true;

    return sourceWriteTime > serializedWriteTime;
}

std::optional<ModelAsset> AssetsLoader::importModelFromSource(const std::string &path)
{
    const std::string extension = extensionLower(path);

    for (const auto &assetLoader : s_assetLoaders)
    {
        if (!assetLoader || !assetLoader->canLoad(extension))
            continue;

        auto modelAsset = assetLoader->load(path);
        auto model = dynamic_cast<ModelAsset *>(modelAsset.get());
        if (model)
            return *model;
    }

    return std::nullopt;
}

std::optional<TextureAsset> AssetsLoader::importTextureFromSource(const std::string &path)
{
    const std::filesystem::path normalizedPath = normalizePath(path);
    const std::string extension = extensionLower(normalizedPath.string());

    TextureAsset textureAsset{};
    textureAsset.name = normalizedPath.stem().string();
    textureAsset.sourcePath = normalizedPath.string();
    textureAsset.assetPath = toTextureAssetPath(normalizedPath).string();

    if (extension == ".dds")
    {
        DDSLoadResult ddsResult{};
        if (!loadDDS(normalizedPath.string(), VK_FORMAT_R8G8B8A8_SRGB, ddsResult))
            return std::nullopt;

        textureAsset.width = ddsResult.width;
        textureAsset.height = ddsResult.height;
        textureAsset.channels = 4u;
        textureAsset.vkFormat = static_cast<uint32_t>(ddsResult.format);
        textureAsset.pixels = std::move(ddsResult.topLevelBytes);
        textureAsset.encoding = ddsResult.compressed ? TextureAsset::PixelEncoding::COMPRESSED_GPU : TextureAsset::PixelEncoding::RGBA8;

        return textureAsset;
    }

    if (extension == ".hdr" || extension == ".exr")
    {
        std::vector<float> pixels;
        int width = 0;
        int height = 0;
        bool loaded = false;

        if (extension == ".exr")
        {
#if defined(ELIX_HAS_OPENEXR)
            loaded = loadFloatPixelsEXR(normalizedPath.string(), pixels, width, height);
#endif
        }

        if (!loaded)
            loaded = loadFloatPixelsSTB(normalizedPath.string(), pixels, width, height);

        if (!loaded)
        {
            VX_ENGINE_ERROR_STREAM("Failed to decode float texture: " << normalizedPath.string() << '\n');
            return std::nullopt;
        }

        textureAsset.width = static_cast<uint32_t>(width);
        textureAsset.height = static_cast<uint32_t>(height);
        textureAsset.channels = 4u;
        textureAsset.encoding = TextureAsset::PixelEncoding::RGBA32F;
        textureAsset.vkFormat = static_cast<uint32_t>(VK_FORMAT_R32G32B32A32_SFLOAT);
        textureAsset.pixels.resize(pixels.size() * sizeof(float));
        std::memcpy(textureAsset.pixels.data(), pixels.data(), textureAsset.pixels.size());

        return textureAsset;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc *pixels = stbi_load(normalizedPath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels)
    {
        VX_ENGINE_ERROR_STREAM("Failed to decode texture: " << normalizedPath.string() << '\n');
        if (const char *failureReason = stbi_failure_reason(); failureReason)
            VX_ENGINE_ERROR_STREAM("stb_image reason: " << failureReason << '\n');
        return std::nullopt;
    }

    const size_t pixelsCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    textureAsset.width = static_cast<uint32_t>(width);
    textureAsset.height = static_cast<uint32_t>(height);
    textureAsset.channels = 4u;
    textureAsset.encoding = TextureAsset::PixelEncoding::RGBA8;
    textureAsset.vkFormat = static_cast<uint32_t>(VK_FORMAT_R8G8B8A8_SRGB);
    textureAsset.pixels.assign(pixels, pixels + pixelsCount);

    stbi_image_free(pixels);

    return textureAsset;
}

bool AssetsLoader::importModelAsset(const std::string &sourcePath, const std::string &outputAssetPath)
{
    const std::filesystem::path normalizedSourcePath = normalizePath(sourcePath);
    if (normalizedSourcePath.empty())
        return false;

    auto importedModel = importModelFromSource(normalizedSourcePath.string());
    if (!importedModel.has_value())
    {
        VX_ENGINE_ERROR_STREAM("Failed to import model source asset: " << normalizedSourcePath.string() << '\n');
        return false;
    }

    const std::filesystem::path normalizedOutputPath = normalizePath(outputAssetPath);
    importedModel->sourcePath = normalizedSourcePath.string();
    importedModel->assetPath = normalizedOutputPath.string();

    AssetsSerializer serializer;
    if (!serializer.writeModel(importedModel.value(), normalizedOutputPath.string()))
    {
        VX_ENGINE_ERROR_STREAM("Failed to serialize model asset: " << normalizedOutputPath.string() << '\n');
        return false;
    }

    return true;
}

bool AssetsLoader::importTextureAsset(const std::string &sourcePath, const std::string &outputAssetPath)
{
    const std::filesystem::path normalizedSourcePath = normalizePath(sourcePath);
    if (normalizedSourcePath.empty())
        return false;

    auto importedTexture = importTextureFromSource(normalizedSourcePath.string());
    if (!importedTexture.has_value())
    {
        VX_ENGINE_ERROR_STREAM("Failed to import texture source asset: " << normalizedSourcePath.string() << '\n');
        return false;
    }

    const std::filesystem::path normalizedOutputPath = normalizePath(outputAssetPath);
    importedTexture->sourcePath = normalizedSourcePath.string();
    importedTexture->assetPath = normalizedOutputPath.string();

    AssetsSerializer serializer;
    if (!serializer.writeTexture(importedTexture.value(), normalizedOutputPath.string()))
    {
        VX_ENGINE_ERROR_STREAM("Failed to serialize texture asset: " << normalizedOutputPath.string() << '\n');
        return false;
    }

    return true;
}

std::optional<MaterialAsset> AssetsLoader::loadMaterial(const std::string &path)
{
    const std::string extension = extensionLower(path);

    for (const auto &assetLoader : s_assetLoaders)
    {
        if (!assetLoader || !assetLoader->canLoad(extension))
            continue;

        auto materialAsset = assetLoader->load(path);
        auto material = dynamic_cast<MaterialAsset *>(materialAsset.get());

        if (material)
            return *material;
    }

    VX_ENGINE_ERROR_STREAM("Failed to load a material\n");

    return std::nullopt;
}

std::optional<ModelAsset> AssetsLoader::loadModel(const std::string &path)
{
    const std::filesystem::path sourcePath = normalizePath(path);
    if (sourcePath.empty())
        return std::nullopt;

    AssetsSerializer serializer;

    if (isElixAssetFile(sourcePath))
    {
        auto model = serializer.readModel(sourcePath.string());
        if (model.has_value())
            return model;

        VX_ENGINE_ERROR_STREAM("Failed to load serialized model asset: " << sourcePath.string() << '\n');
        return std::nullopt;
    }

    const std::filesystem::path serializedPath = toModelAssetPath(sourcePath);

    if (needsReimport(sourcePath, serializedPath))
    {
        auto importedModel = importModelFromSource(sourcePath.string());
        if (!importedModel.has_value())
        {
            VX_ENGINE_ERROR_STREAM("Failed to import model source asset: " << sourcePath.string() << '\n');
            return std::nullopt;
        }

        importedModel->sourcePath = sourcePath.string();
        importedModel->assetPath = serializedPath.string();

        if (!serializer.writeModel(importedModel.value(), serializedPath.string()))
        {
            VX_ENGINE_ERROR_STREAM("Failed to serialize model asset: " << serializedPath.string() << '\n');
            return std::nullopt;
        }
    }

    if (auto serializedModel = serializer.readModel(serializedPath.string()); serializedModel.has_value())
        return serializedModel;

    VX_ENGINE_ERROR_STREAM("Failed to read serialized model asset: " << serializedPath.string() << '\n');
    return std::nullopt;
}

std::optional<TextureAsset> AssetsLoader::loadTexture(const std::string &path)
{
    const std::filesystem::path sourcePath = normalizePath(path);
    if (sourcePath.empty())
        return std::nullopt;

    AssetsSerializer serializer;

    if (isElixAssetFile(sourcePath))
    {
        auto texture = serializer.readTexture(sourcePath.string());
        if (texture.has_value())
            return texture;

        VX_ENGINE_ERROR_STREAM("Failed to load serialized texture asset: " << sourcePath.string() << '\n');
        return std::nullopt;
    }

    const std::filesystem::path serializedPath = toTextureAssetPath(sourcePath);
    if (needsReimport(sourcePath, serializedPath))
    {
        auto importedTexture = importTextureFromSource(sourcePath.string());
        if (!importedTexture.has_value())
            return std::nullopt;

        importedTexture->assetPath = serializedPath.string();
        if (!serializer.writeTexture(importedTexture.value(), serializedPath.string()))
        {
            VX_ENGINE_WARNING_STREAM("Failed to serialize texture asset. Returning imported source texture: " << sourcePath.string() << '\n');
            return importedTexture;
        }
    }

    if (auto serializedTexture = serializer.readTexture(serializedPath.string()); serializedTexture.has_value())
        return serializedTexture;

    VX_ENGINE_WARNING_STREAM("Serialized texture load failed. Falling back to source decode: " << sourcePath.string() << '\n');
    return importTextureFromSource(sourcePath.string());
}

Texture::SharedPtr AssetsLoader::createTextureGPU(const TextureAsset &textureAsset, VkFormat preferredLdrFormat)
{
    if (textureAsset.width == 0u || textureAsset.height == 0u || textureAsset.pixels.empty())
        return nullptr;

    VkFormat format = preferredLdrFormat;
    switch (textureAsset.encoding)
    {
    case TextureAsset::PixelEncoding::RGBA32F:
    {
        if (textureAsset.vkFormat != 0u)
            format = static_cast<VkFormat>(textureAsset.vkFormat);
        else
            format = VK_FORMAT_R32G32B32A32_SFLOAT;
        break;
    }
    case TextureAsset::PixelEncoding::COMPRESSED_GPU:
    {
        if (textureAsset.vkFormat == 0u)
            return nullptr;

        format = static_cast<VkFormat>(textureAsset.vkFormat);
        break;
    }
    case TextureAsset::PixelEncoding::RGBA8:
    default:
    {
        if (format == VK_FORMAT_UNDEFINED)
        {
            if (textureAsset.vkFormat != 0u)
                format = static_cast<VkFormat>(textureAsset.vkFormat);
            else
                format = VK_FORMAT_R8G8B8A8_SRGB;
        }
        break;
    }
    }

    auto texture = std::make_shared<Texture>();
    if (!texture->createFromMemory(textureAsset.pixels.data(),
                                   textureAsset.pixels.size(),
                                   textureAsset.width,
                                   textureAsset.height,
                                   format,
                                   textureAsset.channels))
        return nullptr;

    return texture;
}

Texture::SharedPtr AssetsLoader::loadTextureGPU(const std::string &path, VkFormat preferredLdrFormat)
{
    auto textureAsset = loadTexture(path);
    if (!textureAsset.has_value())
        return nullptr;

    return createTextureGPU(textureAsset.value(), preferredLdrFormat);
}

ELIX_NESTED_NAMESPACE_END
