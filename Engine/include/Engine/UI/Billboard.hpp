#ifndef ELIX_UI_BILLBOARD_HPP
#define ELIX_UI_BILLBOARD_HPP

#include "Core/Macros.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Texture;

ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

/// World-space camera-facing quad game object rendered by UIRenderGraphPass.
/// Typical uses: health bars, nameplates, waypoint markers.
/// World position must be set explicitly (or updated each frame if following an entity).
class Billboard
{
public:
    bool loadTexture(const std::string &assetPath);
    bool ensureTextureLoaded();
    void setTexturePath(const std::string &assetPath);
    void clearTexture();

    const std::string  &getTexturePath() const;
    std::shared_ptr<Texture> getTexture() const;

    void             setWorldPosition(const glm::vec3 &pos);
    const glm::vec3 &getWorldPosition() const;

    void  setSize(float size);
    float getSize() const;

    void  setRotation(float rotationDegrees);
    float getRotation() const;

    void             setColor(const glm::vec4 &color);
    const glm::vec4 &getColor() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

private:
    std::string        m_texturePath;
    std::shared_ptr<Texture> m_texture{nullptr};

    glm::vec3 m_worldPosition{0.0f};
    float     m_size{1.0f};
    float     m_rotation{0.0f};
    glm::vec4 m_color{1.0f};
    bool      m_enabled{true};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_UI_BILLBOARD_HPP
