#ifndef ELIX_PER_FRAME_DATA_WORKER_HPP
#define ELIX_PER_FRAME_DATA_WORKER_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/RenderGraphPassPerFrameData.hpp"
#include "Engine/Camera.hpp"
#include "Engine/Components/LightComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class PerFrameDataWorker
{
public:
    static PerFrameDataWorker begin(RenderGraphPassPerFrameData &data);

    void fillCameraData(Camera *camera);

    bool fillPointLight(Camera *camera, PointLight *pointLight);
    bool fillSpotLight(Camera *camera, SpotLight *spotLight);
    bool fillDirectionalLight(Camera *camera, DirectionalLight *directionalLight);

    void resetPerFrameData();

private:
    PerFrameDataWorker(RenderGraphPassPerFrameData &data);

private:
    RenderGraphPassPerFrameData &m_data;

    const std::array<glm::vec3, ShadowConstants::POINT_SHADOW_FACES> pointFaceDirections{
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)};

    const std::array<glm::vec3, ShadowConstants::POINT_SHADOW_FACES> pointFaceUps{
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f)};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PER_FRAME_DATA_WORKER_HPP