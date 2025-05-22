#ifndef RENDERER_HPP
#define RENDERER_HPP

#include <memory>
#include <vector>
#include <glm/mat4x4.hpp>

#include "ElixirCore/GameObject.hpp"
#include "ElixirCore/ShadowHandler.hpp"

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

    void renderShadowPass(const std::vector<std::shared_ptr<GameObject>>& gameObjects);

    unsigned int getFrameBufferTexture() const;

    void rescaleBuffer(float width, float height);

    void bindBuffer();
    void unbindBuffer();

private:

    // int m_width;
    // int m_height;

    unsigned int m_fbo = 0;
    unsigned int m_colorTexture = 0;
    unsigned int m_depthBuffer = 0;
    unsigned int m_screenQuadVAO = 0;
    unsigned int m_screenQuadVBO = 0;

    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool m_isFrameBufferInitialized{false};
    RendererFrameData m_frameData;
    ShadowHandler m_shadowHandler;
};

inline unsigned int Renderer::getFrameBufferTexture() const
{
    return m_colorTexture;
}


#endif //RENDERER_HPP
