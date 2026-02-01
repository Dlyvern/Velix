#ifndef ELIX_ASSETS_CACHE_HPP
#define ELIX_ASSETS_CACHE_HPP

#include "Core/Macros.hpp"

#include "Engine/Texture.hpp"

#include <unordered_map>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AssetsCache
{
public:
    Texture::SharedPtr addTexture(const std::string &fullPath, Texture::SharedPtr texture);

    Texture::SharedPtr getTexture(const std::string &fullPath);

    const std::unordered_map<std::string, Texture::SharedPtr> &getTextures() const;

private:
    std::unordered_map<std::string, Texture::SharedPtr> m_textures;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_CACHE_HPP