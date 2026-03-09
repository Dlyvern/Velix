#include "Engine/Render/RenderGraph/PerFrameDataWorker.hpp"

#include "Engine/Render/RenderQualitySettings.hpp"
#include "Core/SwapChain.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

PerFrameDataWorker::PerFrameDataWorker(RenderGraphPassPerFrameData &data) : m_data(data)
{
}

void PerFrameDataWorker::fillCameraData(Camera *camera)
{
    m_data.projection = camera ? camera->getProjectionMatrix() : glm::mat4(1.0f);
    m_data.view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
}

bool PerFrameDataWorker::fillDirectionalLight(Camera *camera, DirectionalLight *directionalLight)
{
    const glm::mat4 view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
    const glm::mat3 view3 = glm::mat3(view);
    const auto swapChain = core::VulkanContext::getContext()->getSwapchain();
    glm::vec3 dirWorld = glm::normalize(directionalLight->direction);
    glm::vec3 dirView = glm::normalize(view3 * dirWorld);

    m_data.directionalLightDirection = dirWorld;
    m_data.skyLightEnabled = directionalLight->skyLightEnabled;
    m_data.directionalLightStrength = directionalLight->skyLightEnabled ? directionalLight->strength : 0.0f;

    if (directionalLight->castsShadows)
    {
        const float cameraNear = camera ? std::max(camera->getNear(), 0.01f) : 0.1f;
        const float sceneCameraFar = camera ? std::max(camera->getFar(), cameraNear + 0.1f) : 1000.0f;
        const float shadowMaxDistance = std::max(RenderQualitySettings::getInstance().shadowMaxDistance, cameraNear + 1.0f);
        const float cameraFar = std::min(sceneCameraFar, shadowMaxDistance);
        const float cameraFov = camera ? camera->getFOV() : 60.0f;
        const float cameraAspect = camera ? std::max(camera->getAspect(), 0.001f)
                                          : static_cast<float>(swapChain->getExtent().width) / std::max(1.0f, static_cast<float>(swapChain->getExtent().height));

        glm::mat4 invView = glm::inverse(view);
        glm::vec3 camPos = glm::vec3(invView[3]);
        glm::vec3 camForward = glm::normalize(glm::vec3(invView * glm::vec4(0, 0, -1, 0)));
        glm::vec3 camUp = glm::normalize(glm::vec3(invView * glm::vec4(0, 1, 0, 0)));
        glm::vec3 camRight = glm::normalize(glm::cross(camForward, camUp));
        if (glm::length(camRight) < 0.001f)
            camRight = glm::vec3(1.0f, 0.0f, 0.0f);

        const uint32_t configuredCascadeCount = std::max(1u, std::min(RenderQualitySettings::getInstance().getShadowCascadeCount(), ShadowConstants::MAX_DIRECTIONAL_CASCADES));
        const float cascadeLambda = 0.85f;
        std::array<float, ShadowConstants::MAX_DIRECTIONAL_CASCADES + 1> cascadeDepths{};
        cascadeDepths[0] = cameraNear;

        for (uint32_t cascadeIndex = 0; cascadeIndex < configuredCascadeCount; ++cascadeIndex)
        {
            const float p = static_cast<float>(cascadeIndex + 1) / static_cast<float>(configuredCascadeCount);
            const float logSplit = cameraNear * std::pow(cameraFar / cameraNear, p);
            const float uniformSplit = cameraNear + (cameraFar - cameraNear) * p;
            cascadeDepths[cascadeIndex + 1] = glm::mix(uniformSplit, logSplit, cascadeLambda);
        }

        for (uint32_t cascadeIndex = 0; cascadeIndex < configuredCascadeCount; ++cascadeIndex)
        {
            const float splitNear = cascadeDepths[cascadeIndex];
            const float splitFar = cascadeDepths[cascadeIndex + 1];

            const float tanHalfFov = std::tan(glm::radians(cameraFov * 0.5f));
            const float nearHeight = splitNear * tanHalfFov;
            const float nearWidth = nearHeight * cameraAspect;
            const float farHeight = splitFar * tanHalfFov;
            const float farWidth = farHeight * cameraAspect;

            const glm::vec3 nearCenter = camPos + camForward * splitNear;
            const glm::vec3 farCenter = camPos + camForward * splitFar;

            std::array<glm::vec3, 8> corners{
                nearCenter + camUp * nearHeight - camRight * nearWidth,
                nearCenter + camUp * nearHeight + camRight * nearWidth,
                nearCenter - camUp * nearHeight - camRight * nearWidth,
                nearCenter - camUp * nearHeight + camRight * nearWidth,
                farCenter + camUp * farHeight - camRight * farWidth,
                farCenter + camUp * farHeight + camRight * farWidth,
                farCenter - camUp * farHeight - camRight * farWidth,
                farCenter - camUp * farHeight + camRight * farWidth};

            glm::vec3 cascadeCenter{0.0f};
            for (const auto &corner : corners)
                cascadeCenter += corner;
            cascadeCenter /= static_cast<float>(corners.size());

            // Fit a bounding sphere instead of a tight AABB — rotation-invariant, prevents
            // the ortho bounds from changing as the camera rotates (eliminates one source of shimmer)
            float sphereRadius = 0.0f;
            for (const auto &corner : corners)
                sphereRadius = std::max(sphereRadius, glm::length(corner - cascadeCenter));
            // Round up to a fixed texel-aligned step to reduce frame-to-frame variation
            const float shadowResolutionF = static_cast<float>(RenderQualitySettings::getInstance().getShadowResolution());
            if (shadowResolutionF > 0.0f)
                sphereRadius = std::ceil(sphereRadius * shadowResolutionF / 2.0f) / (shadowResolutionF / 2.0f);

            glm::vec3 lightUp = (std::abs(glm::dot(dirWorld, glm::vec3(0, 1, 0))) > 0.95f)
                                    ? glm::vec3(0, 0, 1)
                                    : glm::vec3(0, 1, 0);
            constexpr float zPadding = 25.0f;
            const float lightDistance = sphereRadius + zPadding + 50.0f;
            glm::mat4 lightView = glm::lookAt(cascadeCenter - dirWorld * lightDistance, cascadeCenter, lightUp);

            // Square ortho projection sized to the sphere — stays constant as camera moves/rotates
            const float cascadeNear = std::max(0.1f, lightDistance - sphereRadius - zPadding);
            const float cascadeFar  = lightDistance + sphereRadius + zPadding;
            glm::mat4 lightProj = glm::ortho(-sphereRadius, sphereRadius, -sphereRadius, sphereRadius, cascadeNear, cascadeFar);
            glm::mat4 lightMatrix = lightProj * lightView;

            // Texel snapping: quantise the world-origin projection to the nearest shadow-map texel
            // so the shadow map doesn't drift by sub-texel amounts as the camera translates
            if (shadowResolutionF > 0.0f)
            {
                glm::vec4 shadowOrigin = lightMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                shadowOrigin *= shadowResolutionF / 2.0f;
                const glm::vec4 roundedOrigin = glm::round(shadowOrigin);
                glm::vec4 roundOffset = (roundedOrigin - shadowOrigin) * (2.0f / shadowResolutionF);
                roundOffset.z = 0.0f;
                roundOffset.w = 0.0f;
                lightProj[3] += roundOffset;
                lightMatrix = lightProj * lightView;
            }

            m_data.directionalLightSpaceMatrices[cascadeIndex] = lightMatrix;
            m_data.directionalCascadeSplits[cascadeIndex] = splitFar;

            if (cascadeIndex == 0)
            {
                m_data.lightSpaceMatrix = lightMatrix;
            }
        }

        m_data.activeDirectionalCascadeCount = configuredCascadeCount;

        return true;
    }

    return false;
}

bool PerFrameDataWorker::fillPointLight(Camera *camera, PointLight *pointLight)
{
    if (pointLight->castsShadows && m_data.activePointShadowCount < ShadowConstants::MAX_POINT_SHADOWS)
    {
        glm::mat4 view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
        glm::vec3 posWorld = pointLight->position;
        const uint32_t pointShadowIndex = m_data.activePointShadowCount++;
        const float nearPlane = 0.1f;
        const float farPlane = std::max(pointLight->radius, nearPlane + 0.1f);
        const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

        for (uint32_t face = 0; face < ShadowConstants::POINT_SHADOW_FACES; ++face)
        {
            const glm::mat4 faceView = glm::lookAt(posWorld, posWorld + pointFaceDirections[face], pointFaceUps[face]);
            const uint32_t matrixIndex = pointShadowIndex * ShadowConstants::POINT_SHADOW_FACES + face;
            m_data.pointLightSpaceMatrices[matrixIndex] = projection * faceView;
        }

        return true;
    }

    return false;
}

bool PerFrameDataWorker::fillSpotLight(Camera *camera, SpotLight *spotLight)
{
    if (spotLight->castsShadows && m_data.activeSpotShadowCount < ShadowConstants::MAX_SPOT_SHADOWS)
    {
        const uint32_t spotShadowIndex = m_data.activeSpotShadowCount++;
        const glm::vec3 positionWorld = spotLight->position;
        const glm::vec3 directionWorld = glm::normalize(spotLight->direction);
        const glm::vec3 up = (std::abs(glm::dot(directionWorld, glm::vec3(0, 1, 0))) > 0.95f)
                                 ? glm::vec3(0, 0, 1)
                                 : glm::vec3(0, 1, 0);

        const float nearPlane = 0.1f;
        const float farPlane = std::max(spotLight->range, nearPlane + 0.1f);
        const float fullConeAngle = std::max(spotLight->outerAngle * 2.0f, 1.0f);
        const glm::mat4 lightView = glm::lookAt(positionWorld, positionWorld + directionWorld, up);
        const glm::mat4 lightProjection = glm::perspective(glm::radians(fullConeAngle), 1.0f, nearPlane, farPlane);
        const glm::mat4 lightMatrix = lightProjection * lightView;

        m_data.spotLightSpaceMatrices[spotShadowIndex] = lightMatrix;

        return true;
    }

    return false;
}

void PerFrameDataWorker::resetPerFrameData()
{
    m_data.lightSpaceMatrix = glm::mat4(1.0f);
    m_data.directionalLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_data.directionalCascadeSplits.fill(std::numeric_limits<float>::max());
    m_data.spotLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_data.pointLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_data.activeDirectionalCascadeCount = 0;
    m_data.activeSpotShadowCount = 0;
    m_data.activePointShadowCount = 0;
    m_data.directionalLightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    m_data.directionalLightStrength = 0.0f;
    m_data.hasDirectionalLight = false;
    m_data.skyLightEnabled = false;
}

PerFrameDataWorker PerFrameDataWorker::begin(RenderGraphPassPerFrameData &data)
{
    return PerFrameDataWorker(data);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END