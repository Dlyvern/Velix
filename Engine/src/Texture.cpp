#include "Engine/Texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
// #define STB_IMAGE_STATIC
// #define STBI_ONLY_HDR
#include <stb_image.h>

#include <volk.h>

#include <iostream>
#include "Core/Buffer.hpp"
#include "Core/VulkanHelpers.hpp"
#include "Core/VulkanContext.hpp"
#include <stdexcept>

#include "Engine/Utilities/BufferUtilities.hpp"
#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Utilities/ImageUtilities.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

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
        std::vector<uint8_t> topLevelBytes;
    };

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
        // Values from DXGI_FORMAT enum (subset needed for DDS textures in asset packs like Bistro)
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
        outResult.topLevelBytes.assign(fileBytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                       fileBytes.begin() + static_cast<std::ptrdiff_t>(offset + topLevelSize));

        if (!compressed && outResult.packing == DDSPixelPacking::BGRA8)
        {
            for (size_t byteOffset = 0; byteOffset + 3u < outResult.topLevelBytes.size(); byteOffset += 4u)
                std::swap(outResult.topLevelBytes[byteOffset + 0u], outResult.topLevelBytes[byteOffset + 2u]);
        }

        return true;
    }

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
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Texture::Texture() = default;

void Texture::createDefaults()
{
    s_whiteTexture = std::make_shared<Texture>();
    s_normalTexture = std::make_shared<Texture>();
    s_ormTexture = std::make_shared<Texture>();
    s_blackTexture = std::make_shared<Texture>();

    s_whiteTexture->createFromPixels(packRGBA8(255, 255, 255, 255), VK_FORMAT_R8G8B8A8_SRGB);

    s_normalTexture->createFromPixels(packRGBA8(128, 128, 255, 255), VK_FORMAT_R8G8B8A8_UNORM);
    s_ormTexture->createFromPixels(packRGBA8(255, 255, 0, 255), VK_FORMAT_R8G8B8A8_UNORM);
    s_blackTexture->createFromPixels(packRGBA8(0, 0, 0, 255), VK_FORMAT_R8G8B8A8_SRGB);
}

void Texture::destroyDefaults()
{
    if (s_whiteTexture)
        s_whiteTexture->destroy();
    if (s_normalTexture)
        s_normalTexture->destroy();
    if (s_ormTexture)
        s_ormTexture->destroy();
    if (s_blackTexture)
        s_blackTexture->destroy();

    s_whiteTexture.reset();
    s_normalTexture.reset();
    s_ormTexture.reset();
    s_blackTexture.reset();
}

void Texture::destroy()
{
    if (m_imageView)
    {
        vkDestroyImageView(m_device, m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }

    freePixels();

    if (m_sampler)
    {
        m_sampler->destroyVk();
        m_sampler.reset();
    }

    if (m_image)
    {
        m_image->destroyVk();
        m_image.reset();
    }
}

Texture::SharedPtr Texture::getDefaultWhiteTexture()
{
    return s_whiteTexture;
}

Texture::SharedPtr Texture::getDefaultNormalTexture()
{
    return s_normalTexture;
}

Texture::SharedPtr Texture::getDefaultOrmTexture()
{
    return s_ormTexture;
}

Texture::SharedPtr Texture::getDefaultBlackTexture()
{
    return s_blackTexture;
}

uint32_t Texture::packRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    // Matches little-endian memory layout used by most desktop CPUs for stb-style RGBA bytes
    return (uint32_t(r) << 0) |
           (uint32_t(g) << 8) |
           (uint32_t(b) << 16) |
           (uint32_t(a) << 24);
}

bool Texture::createFromMemory(const void *pixels, size_t byteCount, uint32_t width, uint32_t height, VkFormat format, uint32_t channels)
{
    if (!pixels || byteCount == 0u || width == 0u || height == 0u)
    {
        VX_ENGINE_ERROR_STREAM("Failed to create texture from memory. Invalid payload.\n");
        return false;
    }

    destroy();

    m_width = static_cast<int>(width);
    m_height = static_cast<int>(height);
    m_channels = static_cast<int>(channels);
    m_device = core::VulkanContext::getContext()->getDevice();

    auto isCompressedFormat = [](VkFormat textureFormat)
    {
        switch (textureFormat)
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
    };

    auto expectedUncompressedByteSize = [width, height](VkFormat textureFormat) -> size_t
    {
        switch (textureFormat)
        {
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
            return static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return static_cast<size_t>(width) * static_cast<size_t>(height) * 8u;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return static_cast<size_t>(width) * static_cast<size_t>(height) * 16u;
        default:
            return 0u;
        }
    };

    if (!isCompressedFormat(format))
    {
        const size_t expectedSize = expectedUncompressedByteSize(format);
        if (expectedSize != 0u && byteCount < expectedSize)
        {
            VX_ENGINE_ERROR_STREAM("Failed to create texture from memory. Payload is too small for format.\n");
            return false;
        }
    }

    const VkExtent2D extent{
        .width = width,
        .height = height};

    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(byteCount);

    auto stagingBuffer = core::Buffer::createShared(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);
    stagingBuffer->upload(pixels, imageSize);

    m_image = core::Image::createShared(extent,
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                        core::memory::MemoryUsage::GPU_ONLY,
                                        format);

    auto commandPool = core::VulkanContext::getContext()->getTransferCommandPool();
    auto queue = core::VulkanContext::getContext()->getTransferQueue();

    auto commandBuffer = core::CommandBuffer::createShared(*commandPool);
    commandBuffer->begin();

    auto firstBarrier = utilities::ImageUtilities::insertImageMemoryBarrier(*m_image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                                            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});

    VkDependencyInfo firstDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    firstDependency.imageMemoryBarrierCount = 1;
    firstDependency.pImageMemoryBarriers = &firstBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &firstDependency);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer->vk(), m_image->vk(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    auto secondBarrier = utilities::ImageUtilities::insertImageMemoryBarrier(*m_image, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                                             {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});

    VkDependencyInfo secondDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    secondDependency.imageMemoryBarrierCount = 1;
    secondDependency.pImageMemoryBarriers = &secondBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &secondDependency);

    commandBuffer->end();

    if (!utilities::AsyncGpuUpload::submit(commandBuffer, queue, {stagingBuffer}))
    {
        VX_ENGINE_ERROR_STREAM("Failed to submit texture upload from memory.\n");
        return false;
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image->vk();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (VkResult result = vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView); result != VK_SUCCESS)
        throw std::runtime_error("Failed to create image view: " + core::helpers::vulkanResultToString(result));

    auto maxAnisotropyLevel = core::VulkanContext::getContext()->getPhysicalDevicePoperties().limits.maxSamplerAnisotropy;
    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_INT_OPAQUE_BLACK, VK_COMPARE_OP_ALWAYS,
                                            VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_TRUE, maxAnisotropyLevel);

    return true;
}

bool Texture::createCubemapFromHDR(const std::string &hdrPath, uint32_t cubemapSize)
{
    int width, height, channels;

    float *hdrData = stbi_loadf(hdrPath.c_str(), &width, &height, &channels, STBI_rgb);

    if (!hdrData)
    {
        VX_ENGINE_ERROR_STREAM("Failed to load HDR image: " << hdrPath << std::endl);
        return false;
    }

    bool result = createCubemapFromEquirectangular(hdrData, width, height, cubemapSize);

    stbi_image_free(hdrData);

    return result;
}

bool Texture::createCubemapFromEquirectangular(const float *data, int width, int height, uint32_t cubemapSize)
{
    m_device = core::VulkanContext::getContext()->getDevice();

    m_width = cubemapSize;
    m_height = cubemapSize;
    VkExtent2D extent{.width = static_cast<uint32_t>(m_width), .height = static_cast<uint32_t>(m_height)};

    m_image = core::Image::createShared(extent,
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        core::memory::MemoryUsage::GPU_ONLY, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                        6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    std::vector<std::vector<float>> faces(6);

    glm::vec3 faceDirections[6][3] = {
        {{1, 0, 0}, {0, 0, -1}, {0, -1, 0}}, // +X (right)
        {{-1, 0, 0}, {0, 0, 1}, {0, -1, 0}}, // -X (left)
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},   // +Y (top)
        {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}}, // -Y (bottom)
        {{0, 0, 1}, {1, 0, 0}, {0, -1, 0}},  // +Z (front)
        {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}} // -Z (back)
    };

    auto commandPool = core::VulkanContext::getContext()->getTransferCommandPool();
    auto queue = core::VulkanContext::getContext()->getTransferQueue();

    auto commandBuffer = core::CommandBuffer::createShared(*commandPool);
    commandBuffer->begin();

    auto firstBarrier = utilities::ImageUtilities::insertImageMemoryBarrier(*m_image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                                            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6});

    VkDependencyInfo firstDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    firstDependency.imageMemoryBarrierCount = 1;
    firstDependency.pImageMemoryBarriers = &firstBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &firstDependency);

    std::vector<core::Buffer::SharedPtr> stagingBuffers;
    stagingBuffers.resize(6);

    for (int face = 0; face < 6; ++face)
    {
        faces[face].resize(cubemapSize * cubemapSize * 4); // RGBA

        for (uint32_t y = 0; y < cubemapSize; ++y)
        {
            for (uint32_t x = 0; x < cubemapSize; ++x)
            {
                // Convert to normalized coordinates [-1, 1]
                float u = (2.0f * (x + 0.5f) / cubemapSize) - 1.0f;
                float v = (2.0f * (y + 0.5f) / cubemapSize) - 1.0f;

                // Calculate direction vector
                glm::vec3 direction =
                    faceDirections[face][0] +
                    u * faceDirections[face][1] +
                    v * faceDirections[face][2];

                direction = glm::normalize(direction);

                // Convert to spherical coordinates
                float phi = atan2(direction.z, direction.x);
                float theta = acos(glm::clamp(direction.y, -1.0f, 1.0f));

                // Convert to UV coordinates
                float u_hdr = phi / (2.0f * glm::pi<float>()) + 0.5f;
                float v_hdr = theta / glm::pi<float>();

                // Sample equirectangular map (with bilinear filtering)
                float x_hdr = u_hdr * width;
                float y_hdr = v_hdr * height;

                int x0 = static_cast<int>(floor(x_hdr));
                int y0 = static_cast<int>(floor(y_hdr));
                int x1 = (x0 + 1) % width;
                int y1 = glm::min(y0 + 1, height - 1);

                float tx = x_hdr - x0;
                float ty = y_hdr - y0;

                // Bilinear interpolation
                glm::vec3 color00, color01, color10, color11;

                for (int c = 0; c < 3; ++c)
                {
                    color00[c] = data[(y0 * width + x0) * 3 + c];
                    color01[c] = data[(y0 * width + x1) * 3 + c];
                    color10[c] = data[(y1 * width + x0) * 3 + c];
                    color11[c] = data[(y1 * width + x1) * 3 + c];
                }

                glm::vec3 color0 = glm::mix(color00, color01, tx);
                glm::vec3 color1 = glm::mix(color10, color11, tx);
                glm::vec3 finalColor = glm::mix(color0, color1, ty);

                int idx = (y * cubemapSize + x) * 4;
                faces[face][idx] = finalColor.r;
                faces[face][idx + 1] = finalColor.g;
                faces[face][idx + 2] = finalColor.b;
                faces[face][idx + 3] = 1.0f;
            }
        }

        VkDeviceSize faceSize = cubemapSize * cubemapSize * 4 * sizeof(float);

        stagingBuffers[face] = core::Buffer::createShared(
            faceSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            core::memory::MemoryUsage::CPU_TO_GPU);

        stagingBuffers[face]->upload(faces[face].data(), faceSize);

        utilities::BufferUtilities::copyBufferToImage(*stagingBuffers[face], *m_image, *commandBuffer,
                                                      VkExtent2D{.width = static_cast<uint32_t>(m_width), .height = static_cast<uint32_t>(m_height)},
                                                      VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, face);
    }

    auto secondBarrier = utilities::ImageUtilities::insertImageMemoryBarrier(*m_image, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                                             {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6});

    VkDependencyInfo secondDependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    secondDependency.imageMemoryBarrierCount = 1;
    secondDependency.pImageMemoryBarriers = &secondBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &secondDependency);
    commandBuffer->end();

    if (!utilities::AsyncGpuUpload::submit(commandBuffer, queue, std::move(stagingBuffers)))
    {
        VX_ENGINE_ERROR_STREAM("Failed to submit cubemap upload\n");
        return false;
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image->vk();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create cubemap image view!");

    m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    return true;
}

void Texture::createFromPixels(uint32_t pixels, VkFormat format)
{
    if (!createFromMemory(&pixels, sizeof(pixels), 1u, 1u, format, 4u))
        throw std::runtime_error("Failed to create default texture from memory");
}

VkSampler Texture::vkSampler()
{
    return m_sampler;
}

VkImageView Texture::vkImageView()
{
    return m_imageView;
}

core::Image::SharedPtr Texture::getImage()
{
    return m_image;
}

Texture::~Texture()
{
    destroy();
}

unsigned char *Texture::getPixels() const
{
    return m_pixels;
}

int Texture::getWidth() const
{
    return m_width;
}

int Texture::getHeight() const
{
    return m_height;
}

int Texture::getChannels() const
{
    return m_channels;
}

void Texture::freePixels()
{
    if (!m_pixels)
        return;

    stbi_image_free(m_pixels);
    m_pixels = nullptr;
}

// TODO does not work
bool Texture::loadCubemap(const std::array<std::string, 6> &cubemaps)
{
    return false;
    // m_device = core::VulkanContext::getContext()->getDevice();

    // std::array<stbi_uc *, 6> facePixels{};

    // for (int i = 0; i < 6; ++i)
    // {
    //     facePixels[i] = stbi_load(cubemaps[i].c_str(), &m_width, &m_height, &m_channels, STBI_rgb_alpha);

    //     if (!facePixels[i])
    //     {
    //         for (int j = 0; j < i; ++j)
    //             stbi_image_free(facePixels[j]);

    //         throw std::runtime_error("failed to load texture image: " + cubemaps[i]);
    //     }

    //     if (i > 0 && (m_width != m_height))
    //     {
    //         for (int j = 0; j <= i; ++j)
    //             stbi_image_free(facePixels[j]);

    //         throw std::runtime_error("cubemap faces must have same dimensions");
    //     }
    // }

    // VkDeviceSize imageSize = m_width * m_height * 4;
    // VkDeviceSize layerSize = imageSize;
    // VkDeviceSize totalSize = layerSize * 6;

    // auto buffer = core::Buffer::createShared(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, core::memory::MemoryUsage::CPU_TO_GPU);

    // void *data;

    // buffer->map(data);

    // for (int face = 0; face < 6; ++face)
    // {
    //     size_t offset = face * layerSize;

    //     memcpy(static_cast<char *>(data) + offset, facePixels[face], layerSize);

    //     if (freePixelsOnLoad)
    //         stbi_image_free(facePixels[face]);
    // }

    // buffer->unmap();

    // commandPool = core::VulkanContext::getContext()->getGraphicsCommandPool();

    // m_image = core::Image::createShared(static_cast<uint32_t>(m_width),
    //                                     static_cast<uint32_t>(m_height), VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, core::memory::MemoryUsage::GPU_ONLY, VK_FORMAT_R8G8B8A8_SRGB,
    //                                     VK_IMAGE_TILING_OPTIMAL, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    // m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    //                                commandPool, core::VulkanContext::getContext()->getGraphicsQueue(), 6);
    // m_image->copyBufferToImage(buffer, static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), commandPool, core::VulkanContext::getContext()->getGraphicsQueue(), 6);
    // m_image->transitionImageLayout(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    //                                commandPool, core::VulkanContext::getContext()->getGraphicsQueue(), 6);

    // VkImageViewCreateInfo imageViewCI{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    // imageViewCI.image = m_image->vk();
    // imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    // imageViewCI.format = VK_FORMAT_R8G8B8A8_SRGB;
    // imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    // imageViewCI.subresourceRange.baseMipLevel = 0;
    // imageViewCI.subresourceRange.levelCount = 1;
    // imageViewCI.subresourceRange.baseArrayLayer = 0;
    // imageViewCI.subresourceRange.layerCount = 6;

    // if (vkCreateImageView(m_device, &imageViewCI, nullptr, &m_imageView) != VK_SUCCESS)
    //     throw std::runtime_error("failed to create cubemap image view!");

    // m_sampler = core::Sampler::createShared(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_BORDER_COLOR_INT_OPAQUE_BLACK);

    // return true;
}

ELIX_NESTED_NAMESPACE_END
