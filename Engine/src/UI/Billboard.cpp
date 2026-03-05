#include "Engine/UI/Billboard.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Core/Logger.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

bool Billboard::loadTexture(const std::string &assetPath)
{
    if (assetPath.empty())
    {
        clearTexture();
        return true;
    }

    auto tex = AssetsLoader::loadTextureGPU(assetPath);
    if (!tex)
    {
        VX_ENGINE_ERROR_STREAM("Billboard: failed to load texture: " << assetPath);
        return false;
    }

    m_texturePath = assetPath;
    m_texture     = std::move(tex);
    return true;
}

bool Billboard::ensureTextureLoaded()
{
    if (m_texture)
        return true;

    if (m_texturePath.empty())
        return false;

    return loadTexture(m_texturePath);
}

void Billboard::setTexturePath(const std::string &assetPath)
{
    m_texturePath = assetPath;
    if (m_texturePath.empty())
        m_texture.reset();
}

void Billboard::clearTexture()
{
    m_texturePath.clear();
    m_texture.reset();
}

const std::string  &Billboard::getTexturePath() const { return m_texturePath; }
Texture::SharedPtr  Billboard::getTexture()     const { return m_texture; }

void Billboard::setWorldPosition(const glm::vec3 &pos) { m_worldPosition = pos; }
const glm::vec3 &Billboard::getWorldPosition() const   { return m_worldPosition; }

void  Billboard::setSize(float size)             { m_size    = size; }
float Billboard::getSize()               const   { return m_size; }

void  Billboard::setRotation(float rotationDegrees) { m_rotation = rotationDegrees; }
float Billboard::getRotation() const { return m_rotation; }

void Billboard::setColor(const glm::vec4 &color) { m_color = color; }
const glm::vec4 &Billboard::getColor()     const { return m_color; }

void Billboard::setEnabled(bool enabled) { m_enabled = enabled; }
bool Billboard::isEnabled()        const { return m_enabled; }

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
