#include "Engine/Render/RenderGraph/PerFrameDataWorker.hpp"
#include "Engine/Assets/AssetManager.hpp"

#include "Core/SwapChain.hpp"
#include "Core/VulkanContext.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/TerrainComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Render/BindlessRegistry.hpp"
#include "Engine/Render/GpuCullingSystem.hpp"
#include "Engine/Render/MeshGeometryRegistry.hpp"
#include "Engine/Render/ObjectIdEncoding.hpp"
#include "Engine/Render/RenderQualitySettings.hpp"
#include "Engine/Render/SceneMaterialResolver.hpp"
#include "Engine/RayTracing/RayTracingGeometryCache.hpp"
#include "Engine/RayTracing/RayTracingScene.hpp"
#include "Engine/RayTracing/SkinnedBlasBuilder.hpp"
#include "Engine/Vertex.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"
#include "Engine/Utilities/BufferUtilities.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

PerFrameDataWorker::PerFrameDataWorker(RenderGraphPassPerFrameData &data, Dependencies dependencies)
    : m_data(data), m_dependencies(std::move(dependencies))
{
}

void PerFrameDataWorker::fillCameraData(Camera *camera)
{
    m_data.projection = camera ? camera->getProjectionMatrix() : glm::mat4(1.0f);
    m_data.view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
}

void PerFrameDataWorker::buildLightData(Scene *scene, Camera *camera)
{
    resetPerFrameData();
    m_lightSpaceMatrixUBO = RenderGraphLightSpaceMatrixUBO{};
    m_lightData.clear();

    if (!scene)
        return;

    const std::vector<std::shared_ptr<BaseLight>> lights = scene->getLights();
    m_lightData.resize(lights.size());

    const glm::mat4 view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
    const glm::mat3 view3 = glm::mat3(view);

    bool directionalShadowAssigned = false;
    bool directionalDataAssigned = false;

    for (size_t i = 0; i < lights.size(); ++i)
    {
        const auto &lightComponent = lights[i];
        auto &lightData = m_lightData[i];

        lightData.position = glm::vec4(0.0f);
        lightData.direction = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        lightData.parameters = glm::vec4(1.0f);
        lightData.colorStrength = glm::vec4(lightComponent->color, lightComponent->strength);
        lightData.shadowInfo = glm::vec4(0.0f);

        if (auto directionalLight = dynamic_cast<DirectionalLight *>(lightComponent.get()))
        {
            m_data.hasDirectionalLight = true;

            const glm::vec3 dirWorld = glm::normalize(directionalLight->direction);
            const glm::vec3 dirView = glm::normalize(view3 * dirWorld);

            lightData.direction = glm::vec4(dirView, 0.0f);
            lightData.parameters.w = 0.0f;

            if (!directionalDataAssigned)
            {
                m_data.directionalLightDirection = dirWorld;
                m_data.skyLightEnabled = directionalLight->skyLightEnabled;
                m_data.directionalLightStrength = directionalLight->skyLightEnabled ? directionalLight->strength : 0.0f;
                directionalDataAssigned = true;

                if (directionalLight->skyLightEnabled)
                {
                    const float sunHeight = glm::clamp(glm::dot(dirWorld, glm::vec3(0.0f, 1.0f, 0.0f)), -1.0f, 1.0f);
                    const float horizonFactor = 1.0f - glm::smoothstep(0.18f, 0.55f, sunHeight);
                    const float belowHorizonFactor = 1.0f - glm::smoothstep(-0.18f, 0.02f, sunHeight);
                    glm::vec3 dynamicColor = glm::mix(glm::vec3(1.00f, 0.96f, 0.89f),
                                                      glm::vec3(1.00f, 0.57f, 0.24f), horizonFactor);
                    dynamicColor = glm::mix(dynamicColor, glm::vec3(1.00f, 0.38f, 0.15f), belowHorizonFactor * 0.75f);
                    directionalLight->color = dynamicColor;
                    lightData.colorStrength = glm::vec4(dynamicColor, lightComponent->strength);
                }
            }

            if (directionalLight->castsShadows && !directionalShadowAssigned && fillDirectionalLight(camera, directionalLight))
            {
                m_lightSpaceMatrixUBO.lightSpaceMatrix = m_data.lightSpaceMatrix;
                m_lightSpaceMatrixUBO.directionalLightSpaceMatrices = m_data.directionalLightSpaceMatrices;
                for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowConstants::MAX_DIRECTIONAL_CASCADES; ++cascadeIndex)
                    m_lightSpaceMatrixUBO.directionalCascadeSplits[cascadeIndex] = m_data.directionalCascadeSplits[cascadeIndex];

                lightData.shadowInfo.x = 1.0f;
                m_data.activeRTShadowLayerCount = std::max(m_data.activeRTShadowLayerCount, static_cast<uint32_t>(i + 1u));
                directionalShadowAssigned = true;
            }
        }
        else if (auto pointLight = dynamic_cast<PointLight *>(lightComponent.get()))
        {
            const glm::vec3 posWorld = lightComponent->position;
            const glm::vec3 posView = glm::vec3(view * glm::vec4(posWorld, 1.0f));

            lightData.position = glm::vec4(posView, 1.0f);
            lightData.parameters.z = pointLight->radius;
            lightData.parameters.w = 2.0f;

            if (pointLight->castsShadows && fillPointLight(camera, pointLight))
            {
                const uint32_t pointShadowIndex = m_data.activePointShadowCount - 1u;
                const float nearPlane = 0.1f;
                const float farPlane = std::max(pointLight->radius, nearPlane + 0.1f);

                lightData.shadowInfo = glm::vec4(1.0f, static_cast<float>(pointShadowIndex), farPlane, nearPlane);
                m_data.activeRTShadowLayerCount = std::max(m_data.activeRTShadowLayerCount, static_cast<uint32_t>(i + 1u));
            }
        }
        else if (auto spotLight = dynamic_cast<SpotLight *>(lightComponent.get()))
        {
            const glm::vec3 posView = glm::vec3(view * glm::vec4(lightComponent->position, 1.0f));
            const glm::vec3 dirView = glm::normalize(view3 * glm::normalize(spotLight->direction));

            lightData.position = glm::vec4(posView, 1.0f);
            lightData.direction = glm::vec4(dirView, 0.0f);
            lightData.parameters.w = 1.0f;
            lightData.parameters.x = glm::cos(glm::radians(spotLight->innerAngle));
            lightData.parameters.y = glm::cos(glm::radians(spotLight->outerAngle));
            lightData.parameters.z = std::max(spotLight->range, 0.1f);

            if (spotLight->castsShadows && fillSpotLight(camera, spotLight))
            {
                const uint32_t spotShadowIndex = m_data.activeSpotShadowCount - 1u;
                m_lightSpaceMatrixUBO.spotLightSpaceMatrices[spotShadowIndex] = m_data.spotLightSpaceMatrices[spotShadowIndex];
                lightData.shadowInfo = glm::vec4(1.0f, static_cast<float>(spotShadowIndex), std::max(spotLight->range, 0.1f), 0.0f);
                m_data.activeRTShadowLayerCount = std::max(m_data.activeRTShadowLayerCount, static_cast<uint32_t>(i + 1u));
            }
        }
    }
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

            float sphereRadius = 0.0f;
            for (const auto &corner : corners)
                sphereRadius = std::max(sphereRadius, glm::length(corner - cascadeCenter));
            const float shadowResolutionF = static_cast<float>(RenderQualitySettings::getInstance().getShadowResolution());
            if (shadowResolutionF > 0.0f)
            {
                const float texelWorldSize = (2.0f * sphereRadius) / shadowResolutionF;
                if (texelWorldSize > 0.0f)
                    sphereRadius = std::ceil(sphereRadius / texelWorldSize) * texelWorldSize;
            }

            glm::vec3 lightUp = (std::abs(glm::dot(dirWorld, glm::vec3(0, 1, 0))) > 0.95f)
                                    ? glm::vec3(0, 0, 1)
                                    : glm::vec3(0, 1, 0);
            const float lightDistance = sphereRadius + 50.0f;
            glm::mat4 lightView = glm::lookAt(cascadeCenter - dirWorld * lightDistance, cascadeCenter, lightUp);

            constexpr float zPadding = 25.0f;
            const float cascadeNear = std::max(0.1f, lightDistance - sphereRadius - zPadding);
            const float cascadeFar = std::max(cascadeNear + 0.1f, lightDistance + sphereRadius + zPadding);
            glm::mat4 lightProj = glm::ortho(-sphereRadius, sphereRadius, -sphereRadius, sphereRadius, cascadeNear, cascadeFar);

            if (shadowResolutionF > 0.0f)
            {
                glm::mat4 lightMatrix = lightProj * lightView;
                glm::vec4 shadowOrigin = lightMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                const float halfRes = shadowResolutionF * 0.5f;
                shadowOrigin *= halfRes;
                const glm::vec4 roundedOrigin = glm::round(shadowOrigin);
                glm::vec4 snapOffset = (roundedOrigin - shadowOrigin) / halfRes;
                snapOffset.z = 0.0f;
                snapOffset.w = 0.0f;
                lightProj[3] += snapOffset;
            }

            glm::mat4 lightMatrix = lightProj * lightView;

            m_data.directionalLightSpaceMatrices[cascadeIndex] = lightMatrix;
            m_data.directionalCascadeSplits[cascadeIndex] = splitFar;

            if (cascadeIndex == 0)
                m_data.lightSpaceMatrix = lightMatrix;
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
        const glm::vec3 posWorld = pointLight->position;
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

void PerFrameDataWorker::pruneRemovedEntities(Scene *scene)
{
    if (!scene)
        return;

    const auto &sceneEntities = scene->getEntities();

    static std::size_t lastEntitiesSize = 0u;
    const std::size_t entitiesSize = sceneEntities.size();
    if (entitiesSize != lastEntitiesSize)
    {
        VX_ENGINE_INFO_STREAM("Scene entity count changed: " << lastEntitiesSize << " -> " << entitiesSize << '\n');
        lastEntitiesSize = entitiesSize;
    }

    std::unordered_set<Entity *> sceneEntitySet;
    sceneEntitySet.reserve(sceneEntities.size());
    for (const auto &entity : sceneEntities)
    {
        if (entity)
            sceneEntitySet.insert(entity.get());
    }

    for (auto it = m_data.drawItems.begin(); it != m_data.drawItems.end();)
    {
        if (sceneEntitySet.find(it->first) == sceneEntitySet.end())
            it = m_data.drawItems.erase(it);
        else
            ++it;
    }
}

void PerFrameDataWorker::syncSceneDrawItems(Scene *scene, const glm::vec3 &cameraPos)
{
    if (!scene)
        return;

    // Load radius: entities within this distance are streamed in.
    // Unload radius adds hysteresis (50 % extra) to avoid load/unload thrashing.
    constexpr float kLoadRadius   = 200.0f;
    constexpr float kUnloadRadius = 300.0f;

    for (const auto &entity : scene->getEntities())
    {
        if (!entity || !entity->isEnabled())
        {
            auto drawItemIt = m_data.drawItems.find(entity.get());
            if (drawItemIt != m_data.drawItems.end())
                m_data.drawItems.erase(drawItemIt);
            continue;
        }

        auto staticMeshComponent  = entity->getComponent<StaticMeshComponent>();
        auto skeletalMeshComponent = entity->getComponent<SkeletalMeshComponent>();
        auto terrainComponent      = entity->getComponent<TerrainComponent>();

        // ---- On-demand streaming: trigger load / unload based on camera distance ----
        if (staticMeshComponent || skeletalMeshComponent)
        {
            // World position from transform (translation column of model matrix).
            glm::vec3 entityPos(0.0f);
            if (auto *t = entity->getComponent<Transform3DComponent>())
            {
                const glm::mat4 m = t->getMatrix();
                entityPos = glm::vec3(m[3]);
            }

            const float dist = glm::distance(cameraPos, entityPos);

            if (staticMeshComponent)
            {
                auto &handle = staticMeshComponent->getModelHandle();
                if (!handle.empty())
                {
                    if (dist <= kLoadRadius && handle.state() == AssetState::Unloaded)
                        AssetManager::getInstance().requestLoad(handle);
                    else if (dist > kUnloadRadius && handle.state() == AssetState::Ready)
                    {
                        staticMeshComponent->clearMeshes();
                        handle.reset();
                    }
                }
                if (handle.ready() && staticMeshComponent->getMeshes().empty())
                    staticMeshComponent->onModelLoaded();
            }

            if (skeletalMeshComponent)
            {
                auto &handle = skeletalMeshComponent->getModelHandle();
                if (!handle.empty())
                {
                    if (dist <= kLoadRadius && handle.state() == AssetState::Unloaded)
                        AssetManager::getInstance().requestLoad(handle);
                    else if (dist > kUnloadRadius && handle.state() == AssetState::Ready)
                    {
                        skeletalMeshComponent->clearMeshes();
                        handle.reset();
                    }
                }
                if (handle.ready() && skeletalMeshComponent->getMeshes().empty())
                    skeletalMeshComponent->onModelLoaded();
            }
        }

        const std::vector<CPUMesh> *meshes = nullptr;
        if (staticMeshComponent && staticMeshComponent->isReady())
            meshes = &staticMeshComponent->getMeshes();
        else if (skeletalMeshComponent && skeletalMeshComponent->isReady())
            meshes = &skeletalMeshComponent->getMeshes();
        else if (terrainComponent)
        {
            terrainComponent->ensureChunkMeshesBuilt();
            meshes = &terrainComponent->getChunkMeshes();
        }

        if (!meshes || meshes->empty())
        {
            auto drawItemIt = m_data.drawItems.find(entity.get());
            if (drawItemIt != m_data.drawItems.end())
                m_data.drawItems.erase(drawItemIt);
            continue;
        }

        auto drawItemIt = m_data.drawItems.find(entity.get());
        if (drawItemIt == m_data.drawItems.end())
            drawItemIt = m_data.drawItems.emplace(entity.get(), DrawItem{}).first;

        auto &drawItem = drawItemIt->second;
        drawItem.transform = entity->hasComponent<Transform3DComponent>()
                                 ? entity->getComponent<Transform3DComponent>()->getMatrix()
                                 : glm::mat4(1.0f);
        drawItem.bonesOffset = 0u;
        updateDrawItemBones(drawItem, entity);
        resizeDrawMeshStates(drawItem, meshes->size());

        for (size_t meshIndex = 0; meshIndex < meshes->size(); ++meshIndex)
        {
            const CPUMesh &sourceMesh = (*meshes)[meshIndex];
            const MeshGeometryInfo &geometryInfo = sourceMesh.getGeometryInfo();
            auto &meshState = drawItem.meshStates[meshIndex];

            if (!meshState.mesh || meshState.geometryHash != geometryInfo.hash)
                meshState.mesh = m_dependencies.meshGeometryRegistry != nullptr
                                     ? m_dependencies.meshGeometryRegistry->createDrawMeshInstance(sourceMesh)
                                     : nullptr;

            meshState.geometryHash = geometryInfo.hash;
            meshState.localBoundsCenter = geometryInfo.localBoundsCenter;
            meshState.localBoundsRadius = geometryInfo.localBoundsRadius;
            meshState.localTransform = computeMeshLocalTransform(sourceMesh, skeletalMeshComponent);

            if (meshState.mesh)
            {
                meshState.mesh->material = resolveMeshMaterial(sourceMesh,
                                                               staticMeshComponent,
                                                               skeletalMeshComponent,
                                                               terrainComponent,
                                                               meshIndex);
            }
        }

        updateWorldBounds(drawItem);
    }
}

void PerFrameDataWorker::buildFrameBones()
{
    m_frameBones.clear();
    m_frameBones.reserve(1024);

    for (auto &[_, drawItem] : m_data.drawItems)
    {
        drawItem.bonesOffset = 0u;

        if (drawItem.finalBones.empty())
            continue;

        drawItem.bonesOffset = static_cast<uint32_t>(m_frameBones.size());
        m_frameBones.insert(m_frameBones.end(), drawItem.finalBones.begin(), drawItem.finalBones.end());
    }
}

void PerFrameDataWorker::buildDrawReferences(const glm::mat4 &view,
                                             const glm::mat4 &projection,
                                             bool enableFrustumCulling)
{
    const std::array<glm::vec4, 6> frustumPlanes = enableFrustumCulling
                                                       ? GpuCullingSystem::extractFrustumPlanes(projection * view)
                                                       : std::array<glm::vec4, 6>{};

    if (m_dependencies.lastFrustumPlanes != nullptr)
        *m_dependencies.lastFrustumPlanes = frustumPlanes;
    if (m_dependencies.lastFrustumCullingEnabled != nullptr)
        *m_dependencies.lastFrustumCullingEnabled = enableFrustumCulling;

    const auto &sfcSettings = RenderQualitySettings::getInstance();
    const bool enableSmallFeatureCulling = sfcSettings.enableSmallFeatureCulling && m_dependencies.enableCpuSmallFeatureCulling;
    const float sfcThreshold = sfcSettings.smallFeatureCullingThreshold;
    const float halfScreenHeight = m_data.swapChainViewport.height * 0.5f;

    m_drawReferences.clear();
    m_drawReferences.reserve(m_data.drawItems.size() * 2u);

    for (auto &[entity, drawItem] : m_data.drawItems)
    {
        const bool hasSkeletonPalette = !drawItem.finalBones.empty();

        for (uint32_t meshIndex = 0; meshIndex < drawItem.meshStates.size(); ++meshIndex)
        {
            auto &meshState = drawItem.meshStates[meshIndex];
            const auto &mesh = meshState.mesh;
            if (!mesh)
                continue;

            const bool meshIsSkinned = hasSkeletonPalette && mesh->vertexLayoutHash == m_skinnedVertexLayoutHash;
            const glm::mat4 modelMatrix = drawItem.transform * meshState.localTransform;

            auto material = mesh->material ? mesh->material : Material::getDefaultMaterial();
            mesh->material = material;

            const glm::vec3 worldBoundsCenter = meshState.worldBoundsCenter;
            const float worldBoundsRadius = meshState.worldBoundsRadius;

            if (enableFrustumCulling &&
                worldBoundsRadius > 0.0f &&
                !GpuCullingSystem::isSphereInsideFrustum(worldBoundsCenter, worldBoundsRadius, frustumPlanes))
            {
                continue;
            }

            if (enableSmallFeatureCulling && worldBoundsRadius > 0.0f)
            {
                const glm::vec4 viewPos = view * glm::vec4(worldBoundsCenter, 1.0f);
                const float depth = -viewPos.z;
                if (depth > 0.0f)
                {
                    const float projectedPixelRadius =
                        worldBoundsRadius * std::abs(projection[1][1]) * halfScreenHeight / depth;
                    if (projectedPixelRadius < sfcThreshold)
                        continue;
                }
            }

            m_drawReferences.push_back(MeshDrawReference{
                .entity = entity,
                .drawItem = &drawItem,
                .meshState = &meshState,
                .meshIndex = meshIndex,
                .mesh = mesh,
                .material = material,
                .worldBoundsCenter = worldBoundsCenter,
                .worldBoundsRadius = worldBoundsRadius,
                .skinned = meshIsSkinned,
                .modelMatrix = modelMatrix});
        }
    }
}

void PerFrameDataWorker::sortDrawReferences(const glm::vec3 &cameraPosition)
{
    std::sort(m_drawReferences.begin(), m_drawReferences.end(),
              [cameraPosition](const MeshDrawReference &left, const MeshDrawReference &right)
              {
                  const bool leftTranslucent = isTranslucentReference(left);
                  const bool rightTranslucent = isTranslucentReference(right);
                  if (leftTranslucent != rightTranslucent)
                      return !leftTranslucent;

                  if (leftTranslucent && rightTranslucent)
                  {
                      const glm::vec3 leftDelta = left.worldBoundsCenter - cameraPosition;
                      const glm::vec3 rightDelta = right.worldBoundsCenter - cameraPosition;
                      const float leftDistanceSquared = glm::dot(leftDelta, leftDelta);
                      const float rightDistanceSquared = glm::dot(rightDelta, rightDelta);
                      if (std::abs(leftDistanceSquared - rightDistanceSquared) > 1e-6f)
                          return leftDistanceSquared > rightDistanceSquared;
                  }

                  if (left.skinned != right.skinned)
                      return left.skinned < right.skinned;

                  if (left.mesh->vertexBuffer.get() != right.mesh->vertexBuffer.get())
                      return left.mesh->vertexBuffer.get() < right.mesh->vertexBuffer.get();

                  if (left.mesh->indexBuffer.get() != right.mesh->indexBuffer.get())
                      return left.mesh->indexBuffer.get() < right.mesh->indexBuffer.get();

                  if (left.material.get() != right.material.get())
                      return left.material.get() < right.material.get();

                  if (left.meshIndex != right.meshIndex)
                      return left.meshIndex < right.meshIndex;

                  const uint32_t leftEntityId = left.entity ? left.entity->getId() : 0u;
                  const uint32_t rightEntityId = right.entity ? right.entity->getId() : 0u;
                  return leftEntityId < rightEntityId;
              });
}

void PerFrameDataWorker::buildRayTracingInputs()
{
    m_data.rtReflectionShadingInstances.clear();

    if (m_dependencies.rayTracingScene == nullptr || m_dependencies.rayTracingGeometryCache == nullptr)
        return;

    const auto &renderSettings = RenderQualitySettings::getInstance();
    const auto *context = core::VulkanContext::getContext().get();
    const bool supportsAccelerationStructures = context && context->hasAccelerationStructureSupport();
    const bool supportsBufferDeviceAddress = context && context->hasBufferDeviceAddressSupport();
    const bool supportsRayQuery = context && context->hasRayQuerySupport();
    const bool supportsRayTracingPipeline = context && context->hasRayTracingPipelineSupport();

    const bool rayTracingPipelineModeActive =
        renderSettings.enableRayTracing &&
        renderSettings.rayTracingMode == RenderQualitySettings::RayTracingMode::Pipeline;
    const bool rayQueryModeActive =
        renderSettings.enableRayTracing &&
        renderSettings.rayTracingMode == RenderQualitySettings::RayTracingMode::RayQuery;

    const bool needsRayTracingScene =
        supportsAccelerationStructures &&
        supportsBufferDeviceAddress &&
        ((rayTracingPipelineModeActive &&
          supportsRayTracingPipeline &&
          (renderSettings.enableRTReflections || renderSettings.enableRTShadows)) ||
         (rayQueryModeActive &&
          supportsRayQuery &&
          (renderSettings.enableRTReflections || renderSettings.enableRTShadows || renderSettings.enableRTAO)));

    if (!needsRayTracingScene)
    {
        m_dependencies.rayTracingScene->clear();
        m_dependencies.rayTracingGeometryCache->clear();
        return;
    }

    std::vector<rayTracing::RayTracingScene::InstanceInput> rtInstances;
    rtInstances.reserve(m_drawReferences.size());

    // Per-instance metadata for RT shading (vertex/index addresses, material params).
    // Index aligns with instance.customInstanceIndex.
    struct RTInstanceMeta
    {
        core::Buffer *vertexBuffer{nullptr};
        core::Buffer *indexBuffer{nullptr};
        uint32_t vertexStride{0};
        const GPUMesh *mesh{nullptr};
    };
    std::vector<RTInstanceMeta> instanceMeta;
    instanceMeta.reserve(m_drawReferences.size());

    for (const auto &reference : m_drawReferences)
    {
        if (!reference.mesh || !reference.material || reference.meshState == nullptr)
            continue;

        const uint32_t materialFlags = reference.material->params().flags;
        const bool isAlphaBlend  = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u;
        const bool isAlphaMask   = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_MASK) != 0u;
        const bool isLegacyGlass = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_LEGACY_GLASS) != 0u;
        const bool isDoubleSided = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_DOUBLE_SIDED) != 0u;

        // Legacy glass and pure alpha-blend objects are excluded entirely from the TLAS.
        if (isLegacyGlass || isAlphaBlend)
            continue;

        rayTracing::RayTracingScene::InstanceInput instance{};
        instance.transform           = reference.modelMatrix;
        instance.customInstanceIndex = static_cast<uint32_t>(rtInstances.size());
        instance.forceOpaque         = !isAlphaMask;
        instance.mask                = isAlphaMask ? 0x02u : 0x01u;
        instance.disableTriangleFacingCull = isDoubleSided;

        RTInstanceMeta meta{};
        meta.mesh = reference.mesh.get();

        if (reference.skinned && m_dependencies.skinnedBlasBuilder)
        {
            // Build / update a skinned BLAS using CPU skinning + BLAS update.
            auto *skeletal = reference.entity
                ? reference.entity->getComponent<SkeletalMeshComponent>()
                : nullptr;
            if (!skeletal)
                continue;
            const auto &cpuMeshes = skeletal->getMeshes();
            if (reference.meshIndex >= cpuMeshes.size())
                continue;
            const CPUMesh &cpuMesh = cpuMeshes[reference.meshIndex];

            // Key: entity ptr ^ (meshIndex * large prime) — unique per (entity, mesh slot)
            const uint64_t blasKey = reinterpret_cast<uint64_t>(reference.entity)
                ^ (static_cast<uint64_t>(reference.meshIndex) * 0x9e3779b97f4a7c15ULL);

            auto blas = m_dependencies.skinnedBlasBuilder->buildOrUpdate(
                blasKey, cpuMesh, m_frameBones, reference.drawItem ? reference.drawItem->bonesOffset : 0u);
            if (!blas)
                continue;

            instance.prebuiltBlas = std::move(blas);
            // mesh / geometryHash not used when prebuiltBlas is set

            // Shading: use the skinned Vertex3D buffer from the builder entry.
            const auto *blasEntry = m_dependencies.skinnedBlasBuilder->find(blasKey);
            if (blasEntry && blasEntry->vertexBuffer && blasEntry->indexBuffer)
            {
                meta.vertexBuffer = blasEntry->vertexBuffer.get();
                meta.indexBuffer  = blasEntry->indexBuffer.get();
                meta.vertexStride = static_cast<uint32_t>(sizeof(vertex::Vertex3D));
            }
        }
        else
        {
            // Static (non-skinned) mesh path.
            if (!reference.meshState->geometryHash.isValid())
                continue;

            instance.geometryHash = reference.meshState->geometryHash;
            instance.mesh         = reference.mesh;

            // Shading buffers come from the geometry cache (filled after TLAS update below).
            meta.vertexBuffer = nullptr; // resolved below
            meta.indexBuffer  = nullptr;
            meta.vertexStride = reference.mesh->vertexStride;
        }

        instanceMeta.push_back(meta);
        rtInstances.push_back(std::move(instance));
    }

    if (m_dependencies.rayTracingScene->update(m_dependencies.currentFrame, rtInstances, *m_dependencies.rayTracingGeometryCache) &&
        m_dependencies.rayTracingScene->getInstanceCount(m_dependencies.currentFrame) > 0)
    {
        m_data.rtReflectionShadingInstances.resize(rtInstances.size());

        for (size_t i = 0; i < rtInstances.size(); ++i)
        {
            const auto &instance = rtInstances[i];
            const auto &meta     = instanceMeta[i];

            if (instance.customInstanceIndex >= m_data.rtReflectionShadingInstances.size())
                continue;

            auto &shadingInstance = m_data.rtReflectionShadingInstances[instance.customInstanceIndex];

            if (instance.prebuiltBlas)
            {
                // Skinned mesh: vertex/index buffers already set in meta.
                if (!meta.vertexBuffer || !meta.indexBuffer)
                    continue;
                shadingInstance.vertexAddress = utilities::BufferUtilities::getBufferDeviceAddress(*meta.vertexBuffer);
                shadingInstance.indexAddress  = utilities::BufferUtilities::getBufferDeviceAddress(*meta.indexBuffer);
                shadingInstance.vertexStride  = meta.vertexStride;
            }
            else
            {
                // Static mesh: look up in geometry cache.
                if (!instance.mesh)
                    continue;
                const auto *entry = m_dependencies.rayTracingGeometryCache->find(instance.geometryHash);
                if (!entry || !entry->rayTracedMesh || !entry->rayTracedMesh->vertexBuffer || !entry->rayTracedMesh->indexBuffer)
                    continue;
                shadingInstance.vertexAddress = utilities::BufferUtilities::getBufferDeviceAddress(*entry->rayTracedMesh->vertexBuffer);
                shadingInstance.indexAddress  = utilities::BufferUtilities::getBufferDeviceAddress(*entry->rayTracedMesh->indexBuffer);
                shadingInstance.vertexStride  = instance.mesh->vertexStride;
            }

            const GPUMesh *mesh = meta.mesh ? meta.mesh : instance.mesh.get();
            if (mesh && mesh->material)
            {
                shadingInstance.material = mesh->material->params();
                if (m_dependencies.bindlessRegistry != nullptr)
                {
                    shadingInstance.material.albedoTexIdx  = m_dependencies.bindlessRegistry->getOrRegisterTexture(mesh->material->getAlbedoTexture().get());
                    shadingInstance.material.normalTexIdx  = m_dependencies.bindlessRegistry->getOrRegisterTexture(mesh->material->getNormalTexture().get());
                    shadingInstance.material.ormTexIdx     = m_dependencies.bindlessRegistry->getOrRegisterTexture(mesh->material->getOrmTexture().get());
                    shadingInstance.material.emissiveTexIdx = m_dependencies.bindlessRegistry->getOrRegisterTexture(mesh->material->getEmissiveTexture().get());
                }
            }
            else
            {
                shadingInstance.material = Material::getDefaultMaterial()->params();
            }
        }
    }
    else
    {
        m_data.rtReflectionShadingInstances.clear();
    }
}

void PerFrameDataWorker::buildRasterBatches()
{
    m_data.perObjectInstances.clear();
    m_data.perObjectInstances.reserve(m_drawReferences.size());
    m_data.drawBatches.clear();
    m_data.drawBatches.reserve(m_drawReferences.size());
    m_data.batchBounds.clear();
    m_data.batchBounds.reserve(m_drawReferences.size());

    for (const auto &reference : m_drawReferences)
    {
        const uint32_t instanceIndex = static_cast<uint32_t>(m_data.perObjectInstances.size());

        const uint32_t materialIndex =
            m_dependencies.bindlessRegistry != nullptr &&
                    m_dependencies.bindlessRegistry->isInitialized() &&
                    reference.material
                ? m_dependencies.bindlessRegistry->getOrRegisterMaterial(reference.material.get())
                : 0u;

        if (m_dependencies.bindlessRegistry != nullptr &&
            m_dependencies.bindlessRegistry->isInitialized() &&
            reference.material &&
            materialIndex < EngineShaderFamilies::MAX_BINDLESS_MATERIALS)
        {
            Material::GPUParams params = reference.material->params();
            params.albedoTexIdx = m_dependencies.bindlessRegistry->getOrRegisterTexture(reference.material->getAlbedoTexture().get());
            params.normalTexIdx = m_dependencies.bindlessRegistry->getOrRegisterTexture(reference.material->getNormalTexture().get());
            params.ormTexIdx = m_dependencies.bindlessRegistry->getOrRegisterTexture(reference.material->getOrmTexture().get());
            params.emissiveTexIdx = m_dependencies.bindlessRegistry->getOrRegisterTexture(reference.material->getEmissiveTexture().get());
            m_dependencies.bindlessRegistry->getCpuMaterialParams()[materialIndex] = params;
        }

        PerObjectInstanceData instanceData{};
        instanceData.model = reference.modelMatrix;
        instanceData.objectInfo = glm::uvec4(
            render::encodeObjectId(reference.entity->getId(), reference.meshIndex),
            reference.drawItem->bonesOffset,
            materialIndex,
            0u);
        m_data.perObjectInstances.push_back(instanceData);

        if (!m_data.drawBatches.empty())
        {
            auto &lastBatch = m_data.drawBatches.back();
            if (lastBatch.skinned == reference.skinned &&
                lastBatch.material.get() == reference.material.get() &&
                hasSameGeometry(lastBatch.mesh, reference.mesh) &&
                lastBatch.firstInstance + lastBatch.instanceCount == instanceIndex)
            {
                ++lastBatch.instanceCount;
                if (reference.worldBoundsRadius > 0.0f && !m_data.batchBounds.empty())
                {
                    GPUBatchBounds &batchBounds = m_data.batchBounds.back();
                    if (batchBounds.radius <= 0.0f)
                    {
                        batchBounds.center = reference.worldBoundsCenter;
                        batchBounds.radius = reference.worldBoundsRadius;
                    }
                    else
                    {
                        const float dist = glm::length(reference.worldBoundsCenter - batchBounds.center);
                        batchBounds.radius = glm::max(batchBounds.radius, dist + reference.worldBoundsRadius);
                    }
                }
                continue;
            }
        }

        DrawBatch batch{};
        batch.mesh = reference.mesh;
        batch.material = reference.material;
        batch.skinned = reference.skinned;
        batch.firstInstance = instanceIndex;
        batch.instanceCount = 1u;
        m_data.drawBatches.push_back(batch);

        GPUBatchBounds batchBounds{};
        batchBounds.center = reference.worldBoundsCenter;
        batchBounds.radius = reference.worldBoundsRadius;
        m_data.batchBounds.push_back(batchBounds);
    }
}

void PerFrameDataWorker::buildShadowBatches()
{
    for (auto &batches : m_data.directionalShadowDrawBatches)
        batches.clear();
    for (auto &batches : m_data.spotShadowDrawBatches)
        batches.clear();
    for (auto &batches : m_data.pointShadowDrawBatches)
        batches.clear();

    m_shadowReferences.clear();
    m_shadowReferences.reserve(m_drawReferences.size());
    for (size_t referenceIndex = 0; referenceIndex < m_drawReferences.size(); ++referenceIndex)
    {
        const uint32_t materialFlags = m_drawReferences[referenceIndex].material ? m_drawReferences[referenceIndex].material->params().flags : 0u;
        const bool isTranslucent = (materialFlags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u;
        if (!isTranslucent)
            m_shadowReferences.push_back(&m_drawReferences[referenceIndex]);
    }

    std::sort(m_shadowReferences.begin(), m_shadowReferences.end(),
              [](const MeshDrawReference *left, const MeshDrawReference *right)
              {
                  if (left->skinned != right->skinned)
                      return left->skinned < right->skinned;

                  if (left->mesh->vertexBuffer.get() != right->mesh->vertexBuffer.get())
                      return left->mesh->vertexBuffer.get() < right->mesh->vertexBuffer.get();

                  if (left->mesh->indexBuffer.get() != right->mesh->indexBuffer.get())
                      return left->mesh->indexBuffer.get() < right->mesh->indexBuffer.get();

                  if (left->meshIndex != right->meshIndex)
                      return left->meshIndex < right->meshIndex;

                  const uint32_t leftEntityId = left->entity ? left->entity->getId() : 0u;
                  const uint32_t rightEntityId = right->entity ? right->entity->getId() : 0u;
                  return leftEntityId < rightEntityId;
              });

    m_shadowPerObjectInstances.clear();
    const uint32_t activeShadowExecutions = m_data.activeDirectionalCascadeCount +
                                            m_data.activeSpotShadowCount +
                                            m_data.activePointShadowCount * ShadowConstants::POINT_SHADOW_FACES;
    m_shadowPerObjectInstances.reserve(m_shadowReferences.size() * std::max<uint32_t>(activeShadowExecutions, 1u));

    constexpr float kTexelCoverageThreshold[] = {0.0f, 1.5f, 2.5f, 4.0f};

    for (uint32_t cascadeIndex = 0; cascadeIndex < m_data.activeDirectionalCascadeCount; ++cascadeIndex)
    {
        const auto &vp = m_data.directionalLightSpaceMatrices[cascadeIndex];
        const auto planes = GpuCullingSystem::extractFrustumPlanes(vp);
        const float threshold = cascadeIndex < 4 ? kTexelCoverageThreshold[cascadeIndex] : 4.0f;
        const float minRadius = shadowTexelWorldSize(vp) * threshold;
        buildShadowBatchesForTarget(m_data.directionalShadowDrawBatches[cascadeIndex], &planes, minRadius);
    }

    for (uint32_t spotIndex = 0; spotIndex < m_data.activeSpotShadowCount; ++spotIndex)
    {
        const auto planes = GpuCullingSystem::extractFrustumPlanes(m_data.spotLightSpaceMatrices[spotIndex]);
        buildShadowBatchesForTarget(m_data.spotShadowDrawBatches[spotIndex], &planes, 0.0f);
    }

    for (uint32_t pointIndex = 0; pointIndex < m_data.activePointShadowCount; ++pointIndex)
    {
        for (uint32_t faceIndex = 0; faceIndex < ShadowConstants::POINT_SHADOW_FACES; ++faceIndex)
        {
            const uint32_t matrixIndex = pointIndex * ShadowConstants::POINT_SHADOW_FACES + faceIndex;
            const auto planes = GpuCullingSystem::extractFrustumPlanes(m_data.pointLightSpaceMatrices[matrixIndex]);
            buildShadowBatchesForTarget(m_data.pointShadowDrawBatches[matrixIndex], &planes, 0.0f);
        }
    }
}

const std::vector<glm::mat4> &PerFrameDataWorker::getFrameBones() const
{
    return m_frameBones;
}

const std::vector<PerObjectInstanceData> &PerFrameDataWorker::getShadowPerObjectInstances() const
{
    return m_shadowPerObjectInstances;
}

const std::vector<RenderGraphLightData> &PerFrameDataWorker::getLightData() const
{
    return m_lightData;
}

const RenderGraphLightSpaceMatrixUBO &PerFrameDataWorker::getLightSpaceMatrixUBO() const
{
    return m_lightSpaceMatrixUBO;
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
    m_lightData.clear();
    m_lightSpaceMatrixUBO = RenderGraphLightSpaceMatrixUBO{};
}

void PerFrameDataWorker::updateDrawItemBones(DrawItem &drawItem, Entity::SharedPtr entity)
{
    if (auto skeletalComponent = entity->getComponent<SkeletalMeshComponent>())
    {
        auto &skeleton = skeletalComponent->getSkeleton();
        if (auto animator = entity->getComponent<AnimatorComponent>(); animator && animator->isAnimationPlaying())
            drawItem.finalBones = skeleton.getFinalMatrices();
        else
            drawItem.finalBones = skeleton.getBindPoses();
    }
    else
    {
        drawItem.finalBones.clear();
    }
}

void PerFrameDataWorker::resizeDrawMeshStates(DrawItem &drawItem, size_t meshCount)
{
    drawItem.meshStates.resize(meshCount);
}

glm::mat4 PerFrameDataWorker::computeMeshLocalTransform(const CPUMesh &mesh, SkeletalMeshComponent *skeletalMeshComponent) const
{
    glm::mat4 meshLocalTransform = mesh.localTransform;

    if (skeletalMeshComponent != nullptr && mesh.attachedBoneId >= 0)
    {
        auto &skeleton = skeletalMeshComponent->getSkeleton();
        if (auto *attachmentBone = skeleton.getBone(mesh.attachedBoneId))
            meshLocalTransform = attachmentBone->finalTransformation * meshLocalTransform;
    }

    return meshLocalTransform;
}

Material::SharedPtr PerFrameDataWorker::resolveMeshMaterial(const CPUMesh &mesh,
                                                            StaticMeshComponent *staticComponent,
                                                            SkeletalMeshComponent *skeletalComponent,
                                                            TerrainComponent *terrainComponent,
                                                            size_t slot)
{
    if (m_dependencies.materialResolver == nullptr)
        return Material::getDefaultMaterial();

    if (staticComponent != nullptr)
    {
        auto overrideMaterial = staticComponent->getMaterialOverride(slot);
        if (!overrideMaterial)
        {
            const std::string &overridePath = staticComponent->getMaterialOverridePath(slot);
            if (!overridePath.empty())
            {
                overrideMaterial = m_dependencies.materialResolver->resolveMaterialOverrideFromPath(overridePath);
                if (overrideMaterial)
                    staticComponent->setMaterialOverride(slot, overrideMaterial);
            }
        }

        if (overrideMaterial)
            return overrideMaterial;
    }
    else if (skeletalComponent != nullptr)
    {
        auto overrideMaterial = skeletalComponent->getMaterialOverride(slot);
        if (!overrideMaterial)
        {
            const std::string &overridePath = skeletalComponent->getMaterialOverridePath(slot);
            if (!overridePath.empty())
            {
                overrideMaterial = m_dependencies.materialResolver->resolveMaterialOverrideFromPath(overridePath);
                if (overrideMaterial)
                    skeletalComponent->setMaterialOverride(slot, overrideMaterial);
            }
        }

        if (overrideMaterial)
            return overrideMaterial;
    }
    else if (terrainComponent != nullptr)
    {
        const std::string &overridePath = terrainComponent->getMaterialOverridePath();
        if (!overridePath.empty())
        {
            auto overrideMaterial = m_dependencies.materialResolver->resolveMaterialOverrideFromPath(overridePath);
            if (overrideMaterial)
                return overrideMaterial;
        }
    }

    return m_dependencies.materialResolver->resolveRuntimeMeshMaterial(mesh);
}

void PerFrameDataWorker::updateWorldBounds(DrawItem &drawItem)
{
    for (auto &meshState : drawItem.meshStates)
    {
        const glm::mat4 meshModel = drawItem.transform * meshState.localTransform;
        meshState.worldBoundsCenter = glm::vec3(meshModel * glm::vec4(meshState.localBoundsCenter, 1.0f));

        const float scaleX = glm::length(glm::vec3(meshModel[0]));
        const float scaleY = glm::length(glm::vec3(meshModel[1]));
        const float scaleZ = glm::length(glm::vec3(meshModel[2]));
        const float maxScale = std::max({scaleX, scaleY, scaleZ, 1.0f});
        meshState.worldBoundsRadius = meshState.localBoundsRadius * maxScale;
    }
}

bool PerFrameDataWorker::hasSameGeometry(const GPUMesh::SharedPtr &left, const GPUMesh::SharedPtr &right)
{
    if (!left || !right)
        return false;

    return left->vertexBuffer.get() == right->vertexBuffer.get() &&
           left->indexBuffer.get() == right->indexBuffer.get() &&
           left->indicesCount == right->indicesCount &&
           left->indexType == right->indexType &&
           left->vertexStride == right->vertexStride &&
           left->vertexLayoutHash == right->vertexLayoutHash;
}

bool PerFrameDataWorker::isTranslucentReference(const MeshDrawReference &reference)
{
    if (!reference.material)
        return false;

    const uint32_t flags = reference.material->params().flags;
    return (flags & Material::MaterialFlags::EMATERIAL_FLAG_ALPHA_BLEND) != 0u;
}

bool PerFrameDataWorker::hasSameShadowKey(const DrawBatch &batch, const MeshDrawReference &reference) const
{
    return batch.skinned == reference.skinned && hasSameGeometry(batch.mesh, reference.mesh);
}

float PerFrameDataWorker::shadowTexelWorldSize(const glm::mat4 &vp) const
{
    const float scaleX = glm::length(glm::vec3(vp[0]));
    if (scaleX < 1e-6f)
        return 0.0f;

    const float shadowResolution = static_cast<float>(std::max(1u, RenderQualitySettings::getInstance().getShadowResolution()));
    return (2.0f / scaleX) / shadowResolution;
}

void PerFrameDataWorker::buildShadowBatchesForTarget(std::vector<DrawBatch> &outBatches,
                                                     const std::array<glm::vec4, 6> *cullPlanes,
                                                     float minMeshRadius)
{
    outBatches.clear();

    for (const MeshDrawReference *reference : m_shadowReferences)
    {
        if (!reference || !reference->mesh)
            continue;

        if (cullPlanes &&
            reference->worldBoundsRadius > 0.0f &&
            !GpuCullingSystem::isSphereInsideFrustum(reference->worldBoundsCenter, reference->worldBoundsRadius, *cullPlanes))
        {
            continue;
        }

        if (minMeshRadius > 0.0f && reference->worldBoundsRadius < minMeshRadius)
            continue;

        const uint32_t instanceIndex = static_cast<uint32_t>(m_shadowPerObjectInstances.size());
        PerObjectInstanceData instanceData{};
        instanceData.model = reference->modelMatrix;
        instanceData.objectInfo = glm::uvec4(
            render::encodeObjectId(reference->entity->getId(), reference->meshIndex),
            reference->drawItem->bonesOffset,
            0u,
            0u);
        m_shadowPerObjectInstances.push_back(instanceData);

        if (!outBatches.empty())
        {
            auto &lastBatch = outBatches.back();
            if (hasSameShadowKey(lastBatch, *reference) &&
                lastBatch.firstInstance + lastBatch.instanceCount == instanceIndex)
            {
                ++lastBatch.instanceCount;
                continue;
            }
        }

        DrawBatch batch{};
        batch.mesh = reference->mesh;
        batch.material = reference->material;
        batch.skinned = reference->skinned;
        batch.firstInstance = instanceIndex;
        batch.instanceCount = 1u;
        outBatches.push_back(batch);
    }
}

PerFrameDataWorker PerFrameDataWorker::begin(RenderGraphPassPerFrameData &data, Dependencies dependencies)
{
    return PerFrameDataWorker(data, std::move(dependencies));
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
