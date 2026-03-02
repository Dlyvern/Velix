#ifndef ELIX_FONT_ATLAS_HPP
#define ELIX_FONT_ATLAS_HPP

#include "Core/Macros.hpp"

#include "Engine/UI/Font.hpp"
#include "Engine/Texture.hpp"

#include <glm/vec4.hpp>

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

/// GPU font atlas: packs all glyphs from a Font into a single RGBA8 texture.
/// The R channel holds the glyph coverage (0 = transparent, 255 = opaque).
class FontAtlas
{
public:
    struct GlyphUV
    {
        float u0{0.0f};
        float v0{0.0f};
        float u1{0.0f};
        float v1{0.0f};
    };

    /// Build the atlas from a loaded Font.  Returns false on failure.
    bool build(const Font &font);

    /// Returns the UV rect for a given character, or a zeroed rect if not found.
    GlyphUV getGlyphUV(char c) const;

    /// Returns the atlas texture (ready for GPU sampling).
    Texture::SharedPtr getTexture() const;

    int getAtlasWidth()  const;
    int getAtlasHeight() const;

    bool isBuilt() const;

private:
    Texture::SharedPtr              m_texture{nullptr};
    std::unordered_map<char, GlyphUV> m_uvMap;
    int m_atlasWidth{0};
    int m_atlasHeight{0};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_FONT_ATLAS_HPP
