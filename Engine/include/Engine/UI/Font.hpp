#ifndef ELIX_FONT_HPP
#define ELIX_FONT_HPP

#include "Core/Macros.hpp"

#include <string>
#include <unordered_map>
#include <vector>

#include "glm/vec2.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

struct Glyph
{
public:
    unsigned int textureID;
    glm::ivec2 size;
    glm::ivec2 bearing;
    unsigned int advance;
    float bitmapWidth;
    float bitmapRows;
    std::vector<unsigned char> bitmapData;
};

class Font
{
public:
    bool load(const std::string &path);
    const Glyph *getGlyph(char c) const;
    const std::string &getFontPath() const;

    glm::vec2 calculateTextSize(const std::string &text, float scale) const;

private:
    std::unordered_map<char, Glyph> m_glyphs;
    std::string m_pathToFont;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_FONT_HPP