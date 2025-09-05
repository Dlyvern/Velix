#ifndef ELIX_TEXTURE_IMAGE_HPP
#define ELIX_TEXTURE_IMAGE_HPP

#include "Macros.hpp"
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class TextureImage
{
public:
    bool load(const std::string& path, bool freeOnLoad = true);
    void free();
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_TEXTURE_IMAGE_HPP