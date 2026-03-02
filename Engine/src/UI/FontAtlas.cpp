#include "Engine/UI/FontAtlas.hpp"

#include "Core/Logger.hpp"

#include <cstring>
#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

bool FontAtlas::build(const Font &font)
{
    // --- Pass 1: measure atlas dimensions ----------------------------------
    int totalWidth  = 0;
    int maxHeight   = 0;
    const int padding = 2; // pixels between glyphs

    // We iterate ASCII printable range matching Font::load()
    for (unsigned char c = 32; c < 127; ++c)
    {
        const Glyph *g = font.getGlyph(static_cast<char>(c));
        if (!g)
            continue;

        totalWidth += static_cast<int>(g->bitmapWidth) + padding;
        maxHeight   = std::max(maxHeight, static_cast<int>(g->bitmapRows));
    }

    if (totalWidth <= 0 || maxHeight <= 0)
    {
        VX_ENGINE_ERROR_STREAM("FontAtlas: no glyph data to build atlas\n");
        return false;
    }

    m_atlasWidth  = totalWidth;
    m_atlasHeight = maxHeight;

    // --- Pass 2: fill RGBA8 atlas buffer -----------------------------------
    // R channel = glyph coverage; G, B, A = 255 so sampling as RGBA still
    // gives correct results when the shader reads .r
    const size_t bufSize = static_cast<size_t>(m_atlasWidth) * static_cast<size_t>(m_atlasHeight) * 4u;
    std::vector<uint8_t> buffer(bufSize, 0u);

    int penX = 0;
    for (unsigned char c = 32; c < 127; ++c)
    {
        const Glyph *g = font.getGlyph(static_cast<char>(c));
        if (!g)
            continue;

        const int gw = static_cast<int>(g->bitmapWidth);
        const int gh = static_cast<int>(g->bitmapRows);

        // Copy glyph bitmap into atlas row by row
        for (int row = 0; row < gh; ++row)
        {
            for (int col = 0; col < gw; ++col)
            {
                const size_t srcIdx = static_cast<size_t>(row * gw + col);
                const size_t dstX   = static_cast<size_t>(penX + col);
                const size_t dstY   = static_cast<size_t>(row);
                const size_t dstIdx = (dstY * static_cast<size_t>(m_atlasWidth) + dstX) * 4u;

                uint8_t coverage = (srcIdx < g->bitmapData.size()) ? g->bitmapData[srcIdx] : 0u;
                buffer[dstIdx + 0] = coverage; // R
                buffer[dstIdx + 1] = coverage; // G
                buffer[dstIdx + 2] = coverage; // B
                buffer[dstIdx + 3] = coverage; // A
            }
        }

        // Record UV rect for this glyph
        GlyphUV uv{};
        uv.u0 = static_cast<float>(penX)      / static_cast<float>(m_atlasWidth);
        uv.u1 = static_cast<float>(penX + gw) / static_cast<float>(m_atlasWidth);
        uv.v0 = 0.0f;
        uv.v1 = (gh > 0) ? static_cast<float>(gh) / static_cast<float>(m_atlasHeight) : 1.0f;

        m_uvMap[static_cast<char>(c)] = uv;
        penX += gw + padding;
    }

    // --- Pass 3: upload to GPU --------------------------------------------
    m_texture = std::make_shared<Texture>();
    if (!m_texture->createFromMemory(buffer.data(), bufSize,
                                     static_cast<uint32_t>(m_atlasWidth),
                                     static_cast<uint32_t>(m_atlasHeight),
                                     VK_FORMAT_R8G8B8A8_UNORM, 4u))
    {
        VX_ENGINE_ERROR_STREAM("FontAtlas: failed to upload atlas texture\n");
        m_texture.reset();
        return false;
    }

    return true;
}

FontAtlas::GlyphUV FontAtlas::getGlyphUV(char c) const
{
    auto it = m_uvMap.find(c);
    return (it != m_uvMap.end()) ? it->second : GlyphUV{};
}

Texture::SharedPtr FontAtlas::getTexture()  const { return m_texture; }
int  FontAtlas::getAtlasWidth()            const { return m_atlasWidth; }
int  FontAtlas::getAtlasHeight()           const { return m_atlasHeight; }
bool FontAtlas::isBuilt()                  const { return m_texture != nullptr; }

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
