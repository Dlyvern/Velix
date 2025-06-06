#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <memory>
#include <vector>
#include <glm/mat4x4.hpp>

#include "ElixirCore/GameObject.hpp"
#include "ElixirCore/ShadowHandler.hpp"
#include "ElixirCore/DebugLine.hpp"

struct RendererFrameData
{
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::vec3 cameraPosition;
};

class Renderer
{
public:
    static Renderer& instance();

    void initFrameBuffer(int width, int height);

    void beginFrame();
    void endFrame();

    [[nodiscard]] const RendererFrameData& getFrameData() const;

    void initShadows();

    void updateLightSpaceMatrix();

    void setSelectedGameObject(GameObject* gameObject);

    void renderShadowPass(const std::vector<std::shared_ptr<GameObject>>& gameObjects);

    unsigned int getFrameBufferTexture() const;

    void rescaleBuffer(float width, float height);

    void bindBuffer();
    void unbindBuffer();

private:
    int m_width{0};
    int m_height{0};
    elix::debug::DebugLine debugLine;
    void updateFrameData();

    GameObject* m_selectedGameObject{nullptr};

    // elix::Texture m_colorTexture;

    unsigned int m_fbo = 0;
    unsigned int m_colorTexture = 0;
    unsigned int m_depthBuffer = 0;

    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    RendererFrameData m_frameData;
    ShadowHandler m_shadowHandler;
};

inline unsigned int Renderer::getFrameBufferTexture() const
{
    return m_colorTexture;
}


#endif //RENDERER_HPP
