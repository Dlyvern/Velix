#ifndef ELIX_FONT_ATLAS_HPP
#define ELIX_FONT_ATLAS_HPP

#include "Core/Macros.hpp"

#include "Engine/UI/Font.hpp"
#include "Engine/Texture.hpp"

#include <glm/vec4.hpp>

#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)



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


    bool build(const Font &font);


    GlyphUV getGlyphUV(char c) const;


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

#endif
