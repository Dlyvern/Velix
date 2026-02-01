#include "Engine/Assets/AssetsCache.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

Texture::SharedPtr AssetsCache::addTexture(const std::string &fullPath, Texture::SharedPtr texture)
{
    m_textures[fullPath] = texture;

    return m_textures[fullPath];
}

const std::unordered_map<std::string, Texture::SharedPtr> &AssetsCache::getTextures() const
{
    return m_textures;
}

Texture::SharedPtr AssetsCache::getTexture(const std::string &fullPath)
{
    auto it = m_textures.find(fullPath);

    return it != m_textures.end() ? it->second : nullptr;
}

ELIX_NESTED_NAMESPACE_END
