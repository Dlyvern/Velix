#include "Engine/Render/RenderGraph/RenderGraph.hpp"
#include "Core/VulkanContext.hpp"
#include "Engine/Render/RenderGraph/RenderGraphDrawProfiler.hpp"

#include <iostream>
#include <array>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_set>
#include <limits>
#include <cmath>

#include "Engine/Components/StaticMeshComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Builders/DescriptorSetBuilder.hpp"
#include "Engine/Assets/AssetsLoader.hpp"

#include "Engine/Utilities/AsyncGpuUpload.hpp"
#include "Engine/Utilities/ImageUtilities.hpp"
#include "Engine/Shaders/ShaderFamily.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct CameraUBO
{
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 invView;
    glm::mat4 invProjection;
};

struct LightData
{
    glm::vec4 position;
    glm::vec4 direction;
    glm::vec4 colorStrength;
    glm::vec4 parameters;
    glm::vec4 shadowInfo; // x = casts shadow, y = shadow index, z = far/range, w = near
};

struct LightSSBO
{
    int lightCount;
    glm::vec3 padding{0.0f};
    LightData lights[];
};

struct LightSpaceMatrixUBO
{
    glm::mat4 lightSpaceMatrix;
    std::array<glm::mat4, elix::engine::ShadowConstants::MAX_DIRECTIONAL_CASCADES> directionalLightSpaceMatrices;
    glm::vec4 directionalCascadeSplits;
    std::array<glm::mat4, elix::engine::ShadowConstants::MAX_SPOT_SHADOWS> spotLightSpaceMatrices;
};

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

RenderGraph::RenderGraph()
{
    m_device = core::VulkanContext::getContext()->getDevice();
    m_swapchain = core::VulkanContext::getContext()->getSwapchain();

    m_commandBuffers.reserve(MAX_FRAMES_IN_FLIGHT);
    m_secondaryCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    m_commandPools.reserve(MAX_FRAMES_IN_FLIGHT);

    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Start signaled
    fenceInfo.pNext = nullptr;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create renderFinished semaphore for image!");
    }
}

void RenderGraph::prepareFrameDataFromScene(Scene *scene)
{
    const auto &sceneEntities = scene->getEntities();

    static std::size_t lastEntitiesSize = 0;
    const std::size_t entitiesSize = sceneEntities.size();

    if (entitiesSize != lastEntitiesSize)
    {
        if (entitiesSize < lastEntitiesSize)
        {
            VX_ENGINE_INFO_STREAM("Entity was deleted");
        }
        else if (entitiesSize > lastEntitiesSize)
        {
            VX_ENGINE_INFO_STREAM("Entity was addded");
        }

        lastEntitiesSize = entitiesSize;
    }

    for (auto it = m_perFrameData.drawItems.begin(); it != m_perFrameData.drawItems.end();)
    {
        if (std::find(sceneEntities.begin(), sceneEntities.end(), it->first) == sceneEntities.end())
            it = m_perFrameData.drawItems.erase(it);
        else
            ++it;
    }

    auto computeMeshGeometryHash = [](const CPUMesh &mesh) -> std::size_t
    {
        std::size_t hashData{0};
        hashing::hash(hashData, mesh.vertexStride);
        hashing::hash(hashData, mesh.vertexLayoutHash);

        for (const auto &vertexByte : mesh.vertexData)
            hashing::hash(hashData, vertexByte);

        for (const auto &index : mesh.indices)
            hashing::hash(hashData, index);

        return hashData;
    };

    auto getOrCreateSharedGeometryMesh = [&](const CPUMesh &mesh) -> GPUMesh::SharedPtr
    {
        const std::size_t hashData = computeMeshGeometryHash(mesh);

        auto it = m_meshes.find(hashData);
        if (it != m_meshes.end())
            return it->second;

        auto createdMesh = GPUMesh::createFromMesh(mesh);
        m_meshes[hashData] = createdMesh;
        return createdMesh;
    };

    auto createDrawMeshInstance = [&](const CPUMesh &mesh) -> GPUMesh::SharedPtr
    {
        auto sharedGeometry = getOrCreateSharedGeometryMesh(mesh);

        auto instance = std::make_shared<GPUMesh>();
        instance->indexBuffer = sharedGeometry->indexBuffer;
        instance->vertexBuffer = sharedGeometry->vertexBuffer;
        instance->indicesCount = sharedGeometry->indicesCount;
        instance->indexType = sharedGeometry->indexType;
        instance->vertexStride = sharedGeometry->vertexStride;
        instance->vertexLayoutHash = sharedGeometry->vertexLayoutHash;

        return instance;
    };

    auto resolveMeshMaterial = [&](const CPUMesh &mesh, StaticMeshComponent *staticComponent, SkeletalMeshComponent *skeletalComponent, size_t slot) -> Material::SharedPtr
    {
        if (staticComponent)
        {
            auto overrideMaterial = staticComponent->getMaterialOverride(slot);
            if (overrideMaterial)
                return overrideMaterial;
        }
        else if (skeletalComponent)
        {
            auto overrideMaterial = skeletalComponent->getMaterialOverride(slot);
            if (overrideMaterial)
                return overrideMaterial;
        }

        if (mesh.material.albedoTexture.empty())
            return Material::getDefaultMaterial();

        auto materialIt = m_materialsByAlbedoPath.find(mesh.material.albedoTexture);
        if (materialIt != m_materialsByAlbedoPath.end())
            return materialIt->second;

        if (m_failedAlbedoTexturePaths.find(mesh.material.albedoTexture) != m_failedAlbedoTexturePaths.end())
            return Material::getDefaultMaterial();

        auto textureImage = AssetsLoader::loadTextureGPU(mesh.material.albedoTexture, VK_FORMAT_R8G8B8A8_SRGB);
        if (!textureImage)
        {
            const auto [_, inserted] = m_failedAlbedoTexturePaths.insert(mesh.material.albedoTexture);
            if (inserted)
            {
                VX_ENGINE_ERROR_STREAM("Failed to load mesh albedo texture: " << mesh.material.albedoTexture << '\n');
                VX_ENGINE_WARNING_STREAM("Using default material for unresolved texture path (cached to avoid per-frame reload attempts)\n");
            }

            auto fallbackMaterial = Material::getDefaultMaterial();
            m_materialsByAlbedoPath[mesh.material.albedoTexture] = fallbackMaterial;
            return fallbackMaterial;
        }

        auto material = Material::create(textureImage);
        m_failedAlbedoTexturePaths.erase(mesh.material.albedoTexture);
        m_materialsByAlbedoPath[mesh.material.albedoTexture] = material;
        return material;
    };

    auto updateDrawItemBones = [](DrawItem &drawItem, Entity::SharedPtr entity)
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
            drawItem.finalBones.clear();
    };

    for (const auto &entity : sceneEntities)
    {
        if (!entity || !entity->isEnabled())
        {
            auto drawItemIt = m_perFrameData.drawItems.find(entity);
            if (drawItemIt != m_perFrameData.drawItems.end())
                m_perFrameData.drawItems.erase(drawItemIt);
            continue;
        }

        auto staticMeshComponent = entity->getComponent<StaticMeshComponent>();
        auto skeletalMeshComponent = entity->getComponent<SkeletalMeshComponent>();

        const std::vector<CPUMesh> *meshes = nullptr;
        if (staticMeshComponent)
            meshes = &staticMeshComponent->getMeshes();
        else if (skeletalMeshComponent)
            meshes = &skeletalMeshComponent->getMeshes();

        if (!meshes || meshes->empty())
        {
            auto drawItemIt = m_perFrameData.drawItems.find(entity);
            if (drawItemIt != m_perFrameData.drawItems.end())
                m_perFrameData.drawItems.erase(drawItemIt);
            continue;
        }

        auto drawItemIt = m_perFrameData.drawItems.find(entity);
        if (drawItemIt == m_perFrameData.drawItems.end())
            drawItemIt = m_perFrameData.drawItems.emplace(entity, DrawItem{}).first;

        auto &drawItem = drawItemIt->second;
        drawItem.transform = entity->hasComponent<Transform3DComponent>() ? entity->getComponent<Transform3DComponent>()->getMatrix() : glm::mat4(1.0f);
        drawItem.bonesOffset = 0;
        updateDrawItemBones(drawItem, entity);

        if (drawItem.meshes.size() != meshes->size())
        {
            drawItem.meshes.clear();
            drawItem.meshes.reserve(meshes->size());

            for (const auto &mesh : *meshes)
                drawItem.meshes.push_back(createDrawMeshInstance(mesh));
        }

        for (size_t meshIndex = 0; meshIndex < meshes->size() && meshIndex < drawItem.meshes.size(); ++meshIndex)
            drawItem.meshes[meshIndex]->material = resolveMeshMaterial((*meshes)[meshIndex], staticMeshComponent, skeletalMeshComponent, meshIndex);
    }

    std::vector<glm::mat4> frameBones;

    for (auto &[_, drawItem] : m_perFrameData.drawItems)
    {
        drawItem.bonesOffset = 0;

        if (drawItem.finalBones.empty())
            continue;

        drawItem.bonesOffset = static_cast<uint32_t>(frameBones.size());
        frameBones.insert(frameBones.end(), drawItem.finalBones.begin(), drawItem.finalBones.end());
    }

    const VkDeviceSize requiredBonesSize = static_cast<VkDeviceSize>(frameBones.size() * sizeof(glm::mat4));
    const VkDeviceSize availableBonesSize = m_bonesSSBOs[m_currentFrame]->getSize();

    if (requiredBonesSize > availableBonesSize)
        throw std::runtime_error("Bones SSBO size is too small for current frame. Increase initial bones buffer size.");

    glm::mat4 *mapped = nullptr;
    m_bonesSSBOs[m_currentFrame]->map(reinterpret_cast<void *&>(mapped));

    if (!frameBones.empty())
        std::memcpy(mapped, frameBones.data(), frameBones.size() * sizeof(glm::mat4));

    m_bonesSSBOs[m_currentFrame]->unmap();
    m_perFrameData.perObjectDescriptorSet = m_perObjectDescriptorSets[m_currentFrame];
}

void RenderGraph::prepareFrame(Camera::SharedPtr camera, Scene *scene, float deltaTime)
{
    prepareFrameDataFromScene(scene);

    m_perFrameData.swapChainViewport = m_swapchain->getViewport();
    m_perFrameData.swapChainScissor = m_swapchain->getScissor();
    m_perFrameData.cameraDescriptorSet = m_cameraDescriptorSets[m_currentFrame];
    m_perFrameData.previewCameraDescriptorSet = m_previewCameraDescriptorSets[m_currentFrame];
    m_perFrameData.deltaTime = deltaTime;

    CameraUBO cameraUBO{};

    cameraUBO.view = camera ? camera->getViewMatrix() : glm::mat4(1.0f);
    cameraUBO.projection = camera ? camera->getProjectionMatrix() : glm::mat4(1.0f);
    cameraUBO.projection[1][1] *= -1;
    cameraUBO.invProjection = glm::inverse(cameraUBO.projection);
    cameraUBO.invView = glm::inverse(cameraUBO.view);

    m_perFrameData.projection = cameraUBO.projection;
    m_perFrameData.view = cameraUBO.view;

    std::memcpy(m_cameraMapped[m_currentFrame], &cameraUBO, sizeof(CameraUBO));

    CameraUBO previewCameraUBO{};
    previewCameraUBO.view = glm::lookAt(
        glm::vec3(0, 0, 3),
        glm::vec3(0, 0, 0),
        glm::vec3(0, 1, 0));

    previewCameraUBO.projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 10.0f);
    previewCameraUBO.projection[1][1] *= -1;

    m_perFrameData.previewProjection = previewCameraUBO.projection;
    m_perFrameData.previewView = previewCameraUBO.view;
    m_perFrameData.skyboxHDRPath = scene ? scene->getSkyboxHDRPath() : std::string{};

    std::memcpy(m_previewCameraMapped[m_currentFrame], &previewCameraUBO, sizeof(CameraUBO));

    // TODO NEEDS TO BE REDESIGNED
    //  size_t requiredSize = sizeof(LightData) * lights.size();

    // if (requiredSize > m_lightSSBOs[m_currentFrame]->getSize()) {
    //     m_lightSSBOs[m_currentFrame] = core::Buffer::create(
    //         requiredSize * 2, // Overallocate by 2x
    //         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    //         0,
    //         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    //     );

    //     // Update descriptor set with new buffer
    //     VkDescriptorBufferInfo bufferInfo{};
    //     bufferInfo.buffer = m_lightSSBOs[frameIndex]->vkBuffer();
    //     bufferInfo.offset = 0;
    //     bufferInfo.range = VK_WHOLE_SIZE;

    //     VkWriteDescriptorSet descriptorWrite{};
    //     descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    //     descriptorWrite.dstSet = m_lightingDescriptorSets[frameIndex];
    //     descriptorWrite.dstBinding = 0;
    //     descriptorWrite.dstArrayElement = 0;
    //     descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    //     descriptorWrite.descriptorCount = 1;
    //     descriptorWrite.pBufferInfo = &bufferInfo;

    //     vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
    // }

    // size_t requiredSize = sizeof(LightData) * (lights.size() * sizeof(LightData));

    const auto lights = scene->getLights();

    void *mapped = nullptr;
    m_lightSSBOs[m_currentFrame]->map(mapped);

    LightSSBO *ssboData = static_cast<LightSSBO *>(mapped);
    ssboData->lightCount = static_cast<int>(lights.size());

    const glm::mat4 view = cameraUBO.view;
    const glm::mat3 view3 = glm::mat3(view);

    m_perFrameData.lightSpaceMatrix = glm::mat4(1.0f);
    m_perFrameData.directionalLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_perFrameData.directionalCascadeSplits.fill(std::numeric_limits<float>::max());
    m_perFrameData.spotLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_perFrameData.pointLightSpaceMatrices.fill(glm::mat4(1.0f));
    m_perFrameData.activeDirectionalCascadeCount = 0;
    m_perFrameData.activeSpotShadowCount = 0;
    m_perFrameData.activePointShadowCount = 0;
    m_perFrameData.directionalLightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
    m_perFrameData.directionalLightStrength = 0.0f;

    LightSpaceMatrixUBO lightSpaceMatrixUBO{};
    lightSpaceMatrixUBO.lightSpaceMatrix = glm::mat4(1.0f);
    for (auto &matrix : lightSpaceMatrixUBO.directionalLightSpaceMatrices)
        matrix = glm::mat4(1.0f);
    lightSpaceMatrixUBO.directionalCascadeSplits = glm::vec4(std::numeric_limits<float>::max());
    for (auto &matrix : lightSpaceMatrixUBO.spotLightSpaceMatrices)
        matrix = glm::mat4(1.0f);

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

    bool directionalShadowAssigned = false;
    bool directionalDataAssigned = false;

    for (size_t i = 0; i < lights.size(); ++i)
    {
        const auto &lightComponent = lights[i];

        ssboData->lights[i].position = glm::vec4(0.0f);
        ssboData->lights[i].direction = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        ssboData->lights[i].parameters = glm::vec4(1.0f);
        ssboData->lights[i].colorStrength = glm::vec4(lightComponent->color, lightComponent->strength);
        ssboData->lights[i].shadowInfo = glm::vec4(0.0f);

        if (auto directionalLight = dynamic_cast<DirectionalLight *>(lightComponent.get()))
        {
            glm::vec3 dirWorld = glm::normalize(directionalLight->direction);
            glm::vec3 dirView = glm::normalize(view3 * dirWorld);

            ssboData->lights[i].direction = glm::vec4(dirView, 0.0f);
            ssboData->lights[i].parameters.w = 0.0f;

            if (!directionalDataAssigned)
            {
                m_perFrameData.directionalLightDirection = dirWorld;
                m_perFrameData.directionalLightStrength = directionalLight->skyLightEnabled ? directionalLight->strength : 0.0f;
                directionalDataAssigned = true;
            }

            if (directionalLight->castsShadows && !directionalShadowAssigned)
            {
                const float cameraNear = camera ? std::max(camera->getNear(), 0.01f) : 0.1f;
                const float cameraFar = camera ? std::max(camera->getFar(), cameraNear + 0.1f) : 1000.0f;
                const float cameraFov = camera ? camera->getFOV() : 60.0f;
                const float cameraAspect = camera ? std::max(camera->getAspect(), 0.001f)
                                                  : static_cast<float>(m_swapchain->getExtent().width) / std::max(1.0f, static_cast<float>(m_swapchain->getExtent().height));

                glm::mat4 invView = glm::inverse(cameraUBO.view);
                glm::vec3 camPos = glm::vec3(invView[3]);
                glm::vec3 camForward = glm::normalize(glm::vec3(invView * glm::vec4(0, 0, -1, 0)));
                glm::vec3 camUp = glm::normalize(glm::vec3(invView * glm::vec4(0, 1, 0, 0)));
                glm::vec3 camRight = glm::normalize(glm::cross(camForward, camUp));
                if (glm::length(camRight) < 0.001f)
                    camRight = glm::vec3(1.0f, 0.0f, 0.0f);

                const float cascadeLambda = 0.85f;
                std::array<float, ShadowConstants::MAX_DIRECTIONAL_CASCADES + 1> cascadeDepths{};
                cascadeDepths[0] = cameraNear;

                for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowConstants::MAX_DIRECTIONAL_CASCADES; ++cascadeIndex)
                {
                    const float p = static_cast<float>(cascadeIndex + 1) / static_cast<float>(ShadowConstants::MAX_DIRECTIONAL_CASCADES);
                    const float logSplit = cameraNear * std::pow(cameraFar / cameraNear, p);
                    const float uniformSplit = cameraNear + (cameraFar - cameraNear) * p;
                    cascadeDepths[cascadeIndex + 1] = glm::mix(uniformSplit, logSplit, cascadeLambda);
                }

                for (uint32_t cascadeIndex = 0; cascadeIndex < ShadowConstants::MAX_DIRECTIONAL_CASCADES; ++cascadeIndex)
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

                    glm::vec3 lightUp = (std::abs(glm::dot(dirWorld, glm::vec3(0, 1, 0))) > 0.95f)
                                            ? glm::vec3(0, 0, 1)
                                            : glm::vec3(0, 1, 0);
                    const float lightDistance = splitFar + 50.0f;
                    glm::mat4 lightView = glm::lookAt(cascadeCenter - dirWorld * lightDistance, cascadeCenter, lightUp);

                    glm::vec3 minBounds(std::numeric_limits<float>::max());
                    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

                    for (const auto &corner : corners)
                    {
                        glm::vec3 cornerLight = glm::vec3(lightView * glm::vec4(corner, 1.0f));
                        minBounds = glm::min(minBounds, cornerLight);
                        maxBounds = glm::max(maxBounds, cornerLight);
                    }

                    constexpr float zPadding = 25.0f;
                    const float cascadeNear = std::max(0.1f, -maxBounds.z - zPadding);
                    const float cascadeFar = std::max(cascadeNear + 0.1f, -minBounds.z + zPadding);
                    glm::mat4 lightProj = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, cascadeNear, cascadeFar);
                    glm::mat4 lightMatrix = lightProj * lightView;

                    m_perFrameData.directionalLightSpaceMatrices[cascadeIndex] = lightMatrix;
                    m_perFrameData.directionalCascadeSplits[cascadeIndex] = splitFar;
                    lightSpaceMatrixUBO.directionalLightSpaceMatrices[cascadeIndex] = lightMatrix;
                    lightSpaceMatrixUBO.directionalCascadeSplits[cascadeIndex] = splitFar;

                    if (cascadeIndex == 0)
                    {
                        m_perFrameData.lightSpaceMatrix = lightMatrix;
                        lightSpaceMatrixUBO.lightSpaceMatrix = lightMatrix;
                    }
                }

                m_perFrameData.activeDirectionalCascadeCount = ShadowConstants::MAX_DIRECTIONAL_CASCADES;
                ssboData->lights[i].shadowInfo.x = 1.0f;
                directionalShadowAssigned = true;
            }
        }
        else if (auto pointLight = dynamic_cast<PointLight *>(lightComponent.get()))
        {
            glm::vec3 posWorld = lightComponent->position;
            glm::vec3 posView = glm::vec3(view * glm::vec4(posWorld, 1.0f));

            ssboData->lights[i].position = glm::vec4(posView, 1.0f);
            ssboData->lights[i].parameters.z = pointLight->radius;
            ssboData->lights[i].parameters.w = 2.0f;

            if (pointLight->castsShadows && m_perFrameData.activePointShadowCount < ShadowConstants::MAX_POINT_SHADOWS)
            {
                const uint32_t pointShadowIndex = m_perFrameData.activePointShadowCount++;
                const float nearPlane = 0.1f;
                const float farPlane = std::max(pointLight->radius, nearPlane + 0.1f);
                const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

                for (uint32_t face = 0; face < ShadowConstants::POINT_SHADOW_FACES; ++face)
                {
                    const glm::mat4 faceView = glm::lookAt(posWorld, posWorld + pointFaceDirections[face], pointFaceUps[face]);
                    const uint32_t matrixIndex = pointShadowIndex * ShadowConstants::POINT_SHADOW_FACES + face;
                    m_perFrameData.pointLightSpaceMatrices[matrixIndex] = projection * faceView;
                }

                ssboData->lights[i].shadowInfo = glm::vec4(1.0f, static_cast<float>(pointShadowIndex), farPlane, nearPlane);
            }
        }
        else if (auto spotLight = dynamic_cast<SpotLight *>(lightComponent.get()))
        {
            glm::vec3 posView = glm::vec3(view * glm::vec4(lightComponent->position, 1.0f));
            glm::vec3 dirView = glm::normalize(view3 * glm::normalize(spotLight->direction));

            ssboData->lights[i].position = glm::vec4(posView, 1.0f);
            ssboData->lights[i].direction = glm::vec4(dirView, 0.0f);

            ssboData->lights[i].parameters.w = 1.0f;
            ssboData->lights[i].parameters.x = glm::cos(glm::radians(spotLight->innerAngle));
            ssboData->lights[i].parameters.y = glm::cos(glm::radians(spotLight->outerAngle));
            ssboData->lights[i].parameters.z = std::max(spotLight->range, 0.1f);

            if (spotLight->castsShadows && m_perFrameData.activeSpotShadowCount < ShadowConstants::MAX_SPOT_SHADOWS)
            {
                const uint32_t spotShadowIndex = m_perFrameData.activeSpotShadowCount++;
                const glm::vec3 positionWorld = lightComponent->position;
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

                m_perFrameData.spotLightSpaceMatrices[spotShadowIndex] = lightMatrix;
                lightSpaceMatrixUBO.spotLightSpaceMatrices[spotShadowIndex] = lightMatrix;
                ssboData->lights[i].shadowInfo = glm::vec4(1.0f, static_cast<float>(spotShadowIndex), farPlane, 0.0f);
            }
        }
    }

    m_lightSSBOs[m_currentFrame]->unmap();
    std::memcpy(m_lightMapped[m_currentFrame], &lightSpaceMatrixUBO, sizeof(LightSpaceMatrixUBO));
}

void RenderGraph::createDescriptorSetPool()
{
    const std::vector<VkDescriptorPoolSize> poolSizes{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 100},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100}};

    // Camera + preview camera + per-object descriptor sets per frame.
    static constexpr uint32_t kRenderGraphSetGroupsPerFrame = 3;
    const uint32_t maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * kRenderGraphSetGroupsPerFrame;

    m_descriptorPool = core::DescriptorPool::createShared(m_device, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                                                          maxSets);
}

void RenderGraph::recreateSwapChain()
{
    m_swapchain->recreate();

    auto barriers = m_renderGraphPassesCompiler.onSwapChainResized(m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

    auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer.begin();

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep.pImageMemoryBarriers = barriers.data();

    vkCmdPipelineBarrier2(commandBuffer, &dep);

    commandBuffer.end();

    commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
    vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());

    for (const auto &[id, renderPass] : m_renderGraphPasses)
        renderPass.renderGraphPass->compile(m_renderGraphPassesStorage);
}

bool RenderGraph::begin()
{
    utilities::AsyncGpuUpload::collectFinished(m_device);

    auto &cpuStageProfilingData = m_cpuStageProfilingByFrame[m_currentFrame];
    cpuStageProfilingData = FrameCpuStageProfilingData{};

    const auto waitForFenceStart = std::chrono::high_resolution_clock::now();
    if (VkResult result = vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX); result != VK_SUCCESS)
    {
        const auto waitForFenceEnd = std::chrono::high_resolution_clock::now();
        cpuStageProfilingData.waitForFenceMs = std::chrono::duration<double, std::milli>(waitForFenceEnd - waitForFenceStart).count();
        VX_ENGINE_ERROR_STREAM("Failed to wait for fences: " << core::helpers::vulkanResultToString(result) << '\n');
        return false;
    }
    const auto waitForFenceEnd = std::chrono::high_resolution_clock::now();
    cpuStageProfilingData.waitForFenceMs = std::chrono::duration<double, std::milli>(waitForFenceEnd - waitForFenceStart).count();

    if (!m_uploadWaitSemaphoresByFrame[m_currentFrame].empty())
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, m_uploadWaitSemaphoresByFrame[m_currentFrame]);
        m_uploadWaitSemaphoresByFrame[m_currentFrame].clear();
    }

    if (VkResult result = vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]); result != VK_SUCCESS)
    {
        VX_ENGINE_ERROR_STREAM("Failed to reset fences: " << core::helpers::vulkanResultToString(result) << '\n');
        return false;
    }

    if (m_hasPendingProfilingResolve[m_currentFrame])
    {
        resolveFrameProfilingData(m_currentFrame);
        m_hasPendingProfilingResolve[m_currentFrame] = false;
    }

    m_commandPools[m_currentFrame]->reset(0);

    const auto acquireImageStart = std::chrono::high_resolution_clock::now();
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain->vk(), UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &m_imageIndex);
    const auto acquireImageEnd = std::chrono::high_resolution_clock::now();
    cpuStageProfilingData.acquireImageMs = std::chrono::duration<double, std::milli>(acquireImageEnd - acquireImageStart).count();

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapChain();

        return false;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        VX_ENGINE_ERROR_STREAM("Failed to acquire swap chain image: " << core::helpers::vulkanResultToString(result) << '\n');
        return false;
    }

    std::vector<uint32_t> dirtyPassIds;
    dirtyPassIds.reserve(m_renderGraphPasses.size());

    for (const auto &[id, renderGraphPass] : m_renderGraphPasses)
    {
        if (renderGraphPass.renderGraphPass->needsRecompilation())
            dirtyPassIds.push_back(renderGraphPass.id);
    }

    if (!dirtyPassIds.empty())
    {
        const auto recompileStart = std::chrono::high_resolution_clock::now();

        vkDeviceWaitIdle(m_device);

        std::unordered_set<uint32_t> dirtyPassIdsSet(dirtyPassIds.begin(), dirtyPassIds.end());
        std::unordered_set<uint32_t> passesToCompile;
        std::queue<uint32_t> pendingPassIds;

        for (const uint32_t dirtyPassId : dirtyPassIds)
            pendingPassIds.push(dirtyPassId);

        while (!pendingPassIds.empty())
        {
            const uint32_t passId = pendingPassIds.front();
            pendingPassIds.pop();

            if (!passesToCompile.insert(passId).second)
                continue;

            auto *passData = findRenderGraphPassById(passId);
            if (!passData)
            {
                VX_ENGINE_ERROR_STREAM("Failed to find pass while resolving recompilation dependencies: " << passId << '\n');
                continue;
            }

            for (const uint32_t dependentPassId : passData->outgoing)
                pendingPassIds.push(dependentPassId);
        }

        std::vector<RGPResourceHandler> resourcesToRecreate;
        resourcesToRecreate.reserve(m_renderGraphPasses.size() * 2);
        std::unordered_set<RGPResourceHandler> uniqueResources;

        for (const uint32_t dirtyPassId : dirtyPassIdsSet)
        {
            const auto *passData = findRenderGraphPassById(dirtyPassId);
            if (!passData)
                continue;

            for (const auto &write : passData->passInfo.writes)
            {
                if (uniqueResources.insert(write.resourceId).second)
                    resourcesToRecreate.push_back(write.resourceId);
            }
        }

        auto barriers = m_renderGraphPassesCompiler.compile(resourcesToRecreate, m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

        if (!barriers.empty())
        {
            auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
            commandBuffer.begin();

            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
            dep.pImageMemoryBarriers = barriers.data();

            vkCmdPipelineBarrier2(commandBuffer, &dep);

            commandBuffer.end();

            commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
            vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
        }

        auto findPassDataByPtr = [&](const IRenderGraphPass *passPtr) -> RenderGraphPassData *
        {
            for (auto &[_, passData] : m_renderGraphPasses)
            {
                if (passData.renderGraphPass.get() == passPtr)
                    return &passData;
            }

            return nullptr;
        };

        for (const auto *sortedPass : m_sortedRenderGraphPasses)
        {
            auto *passData = findPassDataByPtr(sortedPass);
            if (!passData)
            {
                VX_ENGINE_ERROR_STREAM("Failed to find sorted pass while recompiling render graph\n");
                continue;
            }

            if (!passesToCompile.contains(passData->id))
                continue;

            passData->renderGraphPass->compile(m_renderGraphPassesStorage);
        }

        if (m_sortedRenderGraphPasses.empty())
        {
            for (const uint32_t passId : passesToCompile)
            {
                auto *passData = findRenderGraphPassById(passId);
                if (!passData)
                    continue;

                passData->renderGraphPass->compile(m_renderGraphPassesStorage);
            }
        }

        for (const uint32_t dirtyPassId : dirtyPassIdsSet)
        {
            auto *passData = findRenderGraphPassById(dirtyPassId);
            if (passData)
                passData->renderGraphPass->recompilationIsDone();
        }

        const auto recompileEnd = std::chrono::high_resolution_clock::now();
        cpuStageProfilingData.recompileMs = std::chrono::duration<double, std::milli>(recompileEnd - recompileStart).count();
    }

    const auto &primaryCommandBuffer = m_commandBuffers.at(m_currentFrame);

    primaryCommandBuffer->begin();

    auto &currentFramePassProfilingData = m_passExecutionProfilingDataByFrame[m_currentFrame];
    currentFramePassProfilingData.clear();
    m_usedTimestampQueries = 0;
    m_timestampQueryBase = m_currentFrame * m_timestampQueriesPerFrame;
    m_frameQueryRangesByFrame[m_currentFrame] = FrameQueryRange{};

    if (m_isGpuTimingAvailable && m_timestampQueryPool != VK_NULL_HANDLE && m_timestampQueriesPerFrame > 0)
    {
        vkCmdResetQueryPool(primaryCommandBuffer->vk(), m_timestampQueryPool, m_timestampQueryBase, m_timestampQueriesPerFrame);

        auto &frameRange = m_frameQueryRangesByFrame[m_currentFrame];
        frameRange.startQueryIndex = m_timestampQueryBase + m_usedTimestampQueries++;
        vkCmdWriteTimestamp(primaryCommandBuffer->vk(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampQueryPool, frameRange.startQueryIndex);
    }

    size_t passIndex = 0;

    size_t secIndex = 0;

    m_passContextData.currentFrame = m_currentFrame;
    m_passContextData.currentImageIndex = m_imageIndex;

    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
    {
        const auto executions = renderGraphPass->getRenderPassExecutions(m_passContextData);

        for (int recordingIndex = 0; recordingIndex < executions.size(); ++recordingIndex)
        {
            if (secIndex >= m_secondaryCommandBuffers[m_currentFrame].size())
            {
                // Out of preallocated SCBs — allocate more (better to preallocate enough).
                throw std::runtime_error("Not enough secondary command buffers preallocated for frame");
            }

            const auto &execution = executions[recordingIndex];

            const auto &secCB = m_secondaryCommandBuffers[m_currentFrame][secIndex];

            VkCommandBufferInheritanceRenderingInfo someShit{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO_KHR};
            someShit.colorAttachmentCount = static_cast<uint32_t>(execution.colorsRenderingItems.size());
            someShit.pColorAttachmentFormats = execution.colorFormats.data();
            someShit.depthAttachmentFormat = execution.depthFormat;
            someShit.viewMask = 0;
            someShit.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            someShit.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

            VkCommandBufferInheritanceInfo inherit{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
            inherit.pNext = &someShit;
            inherit.subpass = 0;

            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ri.renderArea = execution.renderArea;
            ri.layerCount = 1;
            ri.colorAttachmentCount = static_cast<uint32_t>(execution.colorsRenderingItems.size());
            ri.pColorAttachments = execution.colorsRenderingItems.data();
            ri.pDepthAttachment = execution.useDepth ? &execution.depthRenderingItem : VK_NULL_HANDLE;
            ri.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;

            std::vector<VkImageMemoryBarrier2> firstBarriers;
            std::vector<VkImageMemoryBarrier2> secondBarriers;

            PassExecutionProfilingData executionProfilingData{};
            executionProfilingData.passName = renderGraphPass->getDebugName().empty() ? ("Pass " + std::to_string(passIndex))
                                                                                      : renderGraphPass->getDebugName();

            for (const auto &[id, target] : execution.targets)
            {
                auto textureDescription = m_renderGraphPassesBuilder.getTextureDescription(id);

                auto srcInfoInitial = utilities::ImageUtilities::getSrcLayoutInfo(textureDescription->getInitialLayout());
                auto srcInfoFinal = utilities::ImageUtilities::getSrcLayoutInfo(textureDescription->getFinalLayout());

                auto dstInfoInitial = utilities::ImageUtilities::getDstLayoutInfo(textureDescription->getInitialLayout());
                auto dstInfoFinal = utilities::ImageUtilities::getDstLayoutInfo(textureDescription->getFinalLayout());

                auto aspect = utilities::ImageUtilities::getAspectBasedOnFormat(textureDescription->getFormat());
                auto preBarrier = utilities::ImageUtilities::insertImageMemoryBarrier(
                    *target->getImage(),
                    srcInfoFinal.accessMask,
                    dstInfoInitial.accessMask,
                    textureDescription->getFinalLayout(),   // old
                    textureDescription->getInitialLayout(), // new
                    srcInfoFinal.stageMask,
                    dstInfoInitial.stageMask,
                    {aspect, 0, 1, 0, textureDescription->getArrayLayers()});

                auto postBarrier = utilities::ImageUtilities::insertImageMemoryBarrier(
                    *target->getImage(),
                    srcInfoInitial.accessMask,
                    dstInfoFinal.accessMask,
                    textureDescription->getInitialLayout(), // old
                    textureDescription->getFinalLayout(),   // new
                    srcInfoInitial.stageMask,
                    dstInfoFinal.stageMask,
                    {aspect, 0, 1, 0, textureDescription->getArrayLayers()});

                firstBarriers.push_back(preBarrier);
                secondBarriers.push_back(postBarrier);
            }

            // Keep one query slot for the frame end timestamp.
            if (m_isGpuTimingAvailable && m_timestampQueryPool != VK_NULL_HANDLE && (m_usedTimestampQueries + 2) < m_timestampQueriesPerFrame)
            {
                executionProfilingData.startQueryIndex = m_timestampQueryBase + m_usedTimestampQueries++;
                executionProfilingData.endQueryIndex = m_timestampQueryBase + m_usedTimestampQueries++;

                vkCmdWriteTimestamp(primaryCommandBuffer->vk(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampQueryPool, executionProfilingData.startQueryIndex);
            }

            const auto cpuStartTime = std::chrono::high_resolution_clock::now();

            VkDependencyInfo firstDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            firstDep.imageMemoryBarrierCount = static_cast<uint32_t>(firstBarriers.size());
            firstDep.pImageMemoryBarriers = firstBarriers.data();

            vkCmdPipelineBarrier2(primaryCommandBuffer, &firstDep);

            vkCmdBeginRendering(primaryCommandBuffer, &ri);

            secCB->begin(VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, &inherit);

            {
                profiling::ScopedDrawCallCounter scopedDrawCallCounter(executionProfilingData.drawCalls);
                renderGraphPass->record(secCB, m_perFrameData, m_passContextData);
            }

            secCB->end();

            vkCmdExecuteCommands(primaryCommandBuffer->vk(), 1, secCB->pVk());

            vkCmdEndRendering(primaryCommandBuffer->vk());

            VkDependencyInfo secondDep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            secondDep.imageMemoryBarrierCount = static_cast<uint32_t>(secondBarriers.size());
            secondDep.pImageMemoryBarriers = secondBarriers.data();

            vkCmdPipelineBarrier2(primaryCommandBuffer, &secondDep);

            if (executionProfilingData.endQueryIndex != UINT32_MAX)
                vkCmdWriteTimestamp(primaryCommandBuffer->vk(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, executionProfilingData.endQueryIndex);

            const auto cpuEndTime = std::chrono::high_resolution_clock::now();
            executionProfilingData.cpuTimeMs = std::chrono::duration<double, std::milli>(cpuEndTime - cpuStartTime).count();

            currentFramePassProfilingData.push_back(std::move(executionProfilingData));

            secIndex++;
        }

        ++passIndex;
    }

    auto &frameRange = m_frameQueryRangesByFrame[m_currentFrame];
    if (m_isGpuTimingAvailable &&
        m_timestampQueryPool != VK_NULL_HANDLE &&
        frameRange.startQueryIndex != UINT32_MAX &&
        m_usedTimestampQueries < m_timestampQueriesPerFrame)
    {
        frameRange.endQueryIndex = m_timestampQueryBase + m_usedTimestampQueries++;
        vkCmdWriteTimestamp(primaryCommandBuffer->vk(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampQueryPool, frameRange.endQueryIndex);
    }

    primaryCommandBuffer->end();

    return true;
}

void RenderGraph::end()
{
    const auto &currentCommandBuffer = m_commandBuffers.at(m_currentFrame);
    std::vector<VkSemaphore> waitSemaphores = {m_imageAvailableSemaphores[m_currentFrame]};
    std::vector<VkPipelineStageFlags> waitStages = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    const std::vector<VkSemaphore> signalSemaphores = {m_renderFinishedSemaphores[m_currentFrame]};
    const std::vector<VkSwapchainKHR> swapChains = {m_swapchain->vk()};

    auto &cpuStageProfilingData = m_cpuStageProfilingByFrame[m_currentFrame];

    utilities::AsyncGpuUpload::collectFinished(m_device);
    auto uploadWaitSemaphores = utilities::AsyncGpuUpload::acquireReadySemaphores();
    for (VkSemaphore uploadSemaphore : uploadWaitSemaphores)
    {
        waitSemaphores.push_back(uploadSemaphore);
        waitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    }

    const auto submitStart = std::chrono::high_resolution_clock::now();
    const bool submitOk = currentCommandBuffer->submit(core::VulkanContext::getContext()->getGraphicsQueue(), waitSemaphores, waitStages, signalSemaphores,
                                                       m_inFlightFences[m_currentFrame]);
    const auto submitEnd = std::chrono::high_resolution_clock::now();
    cpuStageProfilingData.submitMs = std::chrono::duration<double, std::milli>(submitEnd - submitStart).count();

    if (!submitOk)
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, uploadWaitSemaphores);
        throw std::runtime_error("Failed to submit render graph command buffer");
    }

    m_uploadWaitSemaphoresByFrame[m_currentFrame] = std::move(uploadWaitSemaphores);

    m_usedTimestampQueriesByFrame[m_currentFrame] = m_usedTimestampQueries;
    m_hasPendingProfilingResolve[m_currentFrame] = true;

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
    presentInfo.pWaitSemaphores = signalSemaphores.data();
    presentInfo.swapchainCount = static_cast<uint32_t>(swapChains.size());
    presentInfo.pSwapchains = swapChains.data();
    presentInfo.pImageIndices = &m_imageIndex;
    presentInfo.pResults = nullptr;

    const auto presentStart = std::chrono::high_resolution_clock::now();
    VkResult result = vkQueuePresentKHR(core::VulkanContext::getContext()->getPresentQueue(), &presentInfo);
    const auto presentEnd = std::chrono::high_resolution_clock::now();
    cpuStageProfilingData.presentMs = std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        recreateSwapChain();
    else if (result != VK_SUCCESS)
        throw std::runtime_error("Failed to present swap chain image: " + core::helpers::vulkanResultToString(result));

    m_perFrameData.additionalData.clear();
}

void RenderGraph::draw()
{
    const auto frameCpuStartTime = std::chrono::high_resolution_clock::now();

    if (begin())
        end();

    const auto frameCpuEndTime = std::chrono::high_resolution_clock::now();
    m_cpuFrameTimesByFrameMs[m_currentFrame] = std::chrono::duration<double, std::milli>(frameCpuEndTime - frameCpuStartTime).count();

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void RenderGraph::setup()
{
    std::vector<RenderGraphPassData *> setupOrder;
    setupOrder.reserve(m_renderGraphPasses.size());

    for (auto &[_, pass] : m_renderGraphPasses)
        setupOrder.push_back(&pass);

    std::sort(setupOrder.begin(), setupOrder.end(), [](const RenderGraphPassData *a, const RenderGraphPassData *b)
              { return a->id < b->id; });

    for (auto *passData : setupOrder)
    {
        if (!passData)
        {
            VX_ENGINE_ERROR_STREAM("failed to find render graph pass during setup\n");
            continue;
        }

        m_renderGraphPassesBuilder.setCurrentPass(&passData->passInfo);
        passData->renderGraphPass->setup(m_renderGraphPassesBuilder);
    }

    compile();
}

void RenderGraph::sortRenderGraphPasses()
{
    auto producerConsumer = [](const RenderGraphPassData &a, const RenderGraphPassData &b)
    {
        for (const auto &wa : a.passInfo.writes)
            for (const auto &rb : b.passInfo.reads)
                if (wa.resourceId == rb.resourceId)
                    return true;
        return false;
    };

    for (auto &[_, renderGraphPass] : m_renderGraphPasses)
    {
        renderGraphPass.outgoing.clear();
        renderGraphPass.indegree = 0;
    }

    auto addEdge = [&](uint32_t srcId, uint32_t dstId)
    {
        if (srcId == dstId)
            return;

        auto *srcPass = findRenderGraphPassById(srcId);
        auto *dstPass = findRenderGraphPassById(dstId);

        if (!srcPass || !dstPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to add graph edge " << srcId << " -> " << dstId << '\n');
            return;
        }

        if (std::find(srcPass->outgoing.begin(), srcPass->outgoing.end(), dstId) != srcPass->outgoing.end())
            return;

        srcPass->outgoing.push_back(dstId);
        dstPass->indegree++;
    };

    for (auto &[_, renderGraphPass] : m_renderGraphPasses)
    {
        for (auto &[_, secondRenderGraphPass] : m_renderGraphPasses)
        {
            if (renderGraphPass.id == secondRenderGraphPass.id)
                continue;

            if (producerConsumer(renderGraphPass, secondRenderGraphPass))
                addEdge(renderGraphPass.id, secondRenderGraphPass.id);
        }
    }

    std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> q;
    std::vector<uint32_t> sorted;

    for (auto &[id, renderGraphPass] : m_renderGraphPasses)
        if (renderGraphPass.indegree <= 0)
            q.push(renderGraphPass.id);

    while (!q.empty())
    {
        uint32_t n = q.top();
        q.pop();
        sorted.push_back(n);

        auto renderGraphPass = findRenderGraphPassById(n);

        if (!renderGraphPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to find pass. Error...\n");
            continue;
        }

        for (uint32_t dst : renderGraphPass->outgoing)
        {
            auto dstRenderGraphPass = findRenderGraphPassById(dst);

            if (!dstRenderGraphPass)
            {
                VX_ENGINE_ERROR_STREAM("Failed to find dst pass. Error...\n");
                continue;
            }

            if (--dstRenderGraphPass->indegree == 0)
                q.push(dst);
        }
    }

    if (sorted.size() != m_renderGraphPasses.size())
    {
        VX_ENGINE_ERROR_STREAM("Failed to build graph tree\n");
        return;
    }

    m_sortedRenderGraphPasses.clear();
    m_sortedRenderGraphPasses.reserve(sorted.size());

    for (const auto &sortId : sorted)
    {
        auto renderGraphPass = findRenderGraphPassById(sortId);

        if (!renderGraphPass)
        {
            VX_ENGINE_ERROR_STREAM("Failed to find sorted node\n");
            continue;
        }

        m_sortedRenderGraphPasses.push_back(renderGraphPass->renderGraphPass.get());
    }

    for (const auto &renderGraphPass : m_sortedRenderGraphPasses)
        VX_ENGINE_INFO_STREAM("Node: " << renderGraphPass->getDebugName());
}

void RenderGraph::compile()
{
    auto barriers = m_renderGraphPassesCompiler.compile(m_renderGraphPassesBuilder, m_renderGraphPassesStorage);

    // VX_ENGINE_INFO_STREAM("Memory before render graphs compile: " << core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM() << '\n');

    auto commandBuffer = core::CommandBuffer::create(*core::VulkanContext::getContext()->getGraphicsCommandPool());
    commandBuffer.begin();

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dep.pImageMemoryBarriers = barriers.data();

    vkCmdPipelineBarrier2(commandBuffer, &dep);

    commandBuffer.end();

    commandBuffer.submit(core::VulkanContext::getContext()->getGraphicsQueue());
    vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());

    for (const auto &[id, pass] : m_renderGraphPasses)
        pass.renderGraphPass->compile(m_renderGraphPassesStorage);

    core::VulkanContext::getContext()->getSwapchain()->getWindow().addResizeCallback([this](platform::Window *, int, int)
                                                                                     { recreateSwapChain(); });

    // VX_ENGINE_INFO_STREAM("Memory after render graphs compile: " << core::VulkanContext::getContext()->getDevice()->getTotalAllocatedVRAM() << '\n');
}

void RenderGraph::createPreviewCameraDescriptorSets()
{
    m_previewCameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_previewCameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_previewCameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &cameraBuffer = m_previewCameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_previewCameraMapped[i]);

        m_previewCameraDescriptorSets[i] = DescriptorSetBuilder::begin()
                                               .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                               .build(m_device, m_descriptorPool, EngineShaderFamilies::cameraDescriptorSetLayout->vk());
    }
}

void RenderGraph::createPerObjectDescriptorSets()
{
    m_perObjectDescriptorSets.resize(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    m_bonesSSBOs.reserve(RenderGraph::MAX_FRAMES_IN_FLIGHT);

    static constexpr VkDeviceSize bonesStructSize = sizeof(glm::mat4);
    static constexpr uint8_t INIT_BONES_COUNT = 100;
    static constexpr VkDeviceSize INITIAL_SIZE = bonesStructSize * (INIT_BONES_COUNT * bonesStructSize);

    for (size_t i = 0; i < RenderGraph::MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto ssboBuffer = m_bonesSSBOs.emplace_back(core::Buffer::createShared(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                               core::memory::MemoryUsage::CPU_TO_GPU));

        m_perObjectDescriptorSets[i] = DescriptorSetBuilder::begin()
                                           .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                           .build(core::VulkanContext::getContext()->getDevice(), m_descriptorPool, EngineShaderFamilies::objectDescriptorSetLayout->vk());
    }
}

void RenderGraph::createCameraDescriptorSets()
{
    m_cameraDescriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    m_cameraMapped.resize(MAX_FRAMES_IN_FLIGHT);
    m_lightMapped.resize(MAX_FRAMES_IN_FLIGHT);

    m_lightSpaceMatrixUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_cameraUniformObjects.reserve(MAX_FRAMES_IN_FLIGHT);
    m_lightSSBOs.reserve(MAX_FRAMES_IN_FLIGHT);

    static constexpr uint8_t INIT_LIGHTS_COUNT = 2;
    static constexpr VkDeviceSize INITIAL_SIZE = sizeof(LightData) * (INIT_LIGHTS_COUNT * sizeof(LightData));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        auto &cameraBuffer = m_cameraUniformObjects.emplace_back(core::Buffer::createShared(sizeof(CameraUBO),
                                                                                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto &lightBuffer = m_lightSpaceMatrixUniformObjects.emplace_back(core::Buffer::createShared(sizeof(LightSpaceMatrixUBO),
                                                                                                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, core::memory::MemoryUsage::CPU_TO_GPU));

        auto ssboBuffer = m_lightSSBOs.emplace_back(core::Buffer::createShared(INITIAL_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                               core::memory::MemoryUsage::CPU_TO_GPU));

        cameraBuffer->map(m_cameraMapped[i]);
        lightBuffer->map(m_lightMapped[i]);

        m_cameraDescriptorSets[i] = DescriptorSetBuilder::begin()
                                        .addBuffer(cameraBuffer, sizeof(CameraUBO), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                        .addBuffer(lightBuffer, sizeof(LightSpaceMatrixUBO), 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                                        .addBuffer(ssboBuffer, VK_WHOLE_SIZE, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                        .build(m_device, m_descriptorPool, EngineShaderFamilies::cameraDescriptorSetLayout->vk());
    }

    createPreviewCameraDescriptorSets();
    createPerObjectDescriptorSets();
}

void RenderGraph::createRenderGraphResources()
{
    createDescriptorSetPool();
    createCameraDescriptorSets();

    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
    {
        auto commandPool = m_commandPools.emplace_back(core::CommandPool::createShared(core::VulkanContext::getContext()->getDevice(),
                                                                                       core::VulkanContext::getContext()->getGraphicsFamily()));

        m_commandBuffers.emplace_back(core::CommandBuffer::createShared(*commandPool));

        m_secondaryCommandBuffers[frame].resize(MAX_RENDER_JOBS);

        for (size_t job = 0; job < MAX_RENDER_JOBS; ++job)
            m_secondaryCommandBuffers[frame][job] = core::CommandBuffer::createShared(*commandPool, VK_COMMAND_BUFFER_LEVEL_SECONDARY);
    }

    sortRenderGraphPasses();
    initTimestampQueryPool();
}

void RenderGraph::cleanResources()
{
    utilities::AsyncGpuUpload::flush(m_device);

    vkQueueWaitIdle(core::VulkanContext::getContext()->getGraphicsQueue());
    vkDeviceWaitIdle(m_device);

    for (auto &uploadSemaphores : m_uploadWaitSemaphoresByFrame)
    {
        utilities::AsyncGpuUpload::releaseSemaphores(m_device, uploadSemaphores);
        uploadSemaphores.clear();
    }

    for (const auto &primary : m_commandBuffers)
        primary->destroyVk();
    for (const auto &secondary : m_secondaryCommandBuffers)
        for (const auto &cb : secondary)
            cb->destroyVk();

    for (const auto &renderPass : m_renderGraphPasses)
        renderPass.second.renderGraphPass->cleanup();

    for (auto &semaphore : m_imageAvailableSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }

    for (auto &semaphore : m_renderFinishedSemaphores)
    {
        if (semaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_device, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }

    for (auto &fence : m_inFlightFences)
    {
        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
    }

    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();

    destroyTimestampQueryPool();
    for (auto &framePassProfilingData : m_passExecutionProfilingDataByFrame)
        framePassProfilingData.clear();
    m_hasPendingProfilingResolve.fill(false);
    m_usedTimestampQueriesByFrame.fill(0);
    m_frameQueryRangesByFrame.fill(FrameQueryRange{});
    m_cpuFrameTimesByFrameMs.fill(0.0);
    m_cpuStageProfilingByFrame.fill(FrameCpuStageProfilingData{});
    m_lastFrameProfilingData = {};

    m_perFrameData.drawItems.clear();
    m_meshes.clear();
    m_materialsByAlbedoPath.clear();
    m_failedAlbedoTexturePaths.clear();
    m_renderGraphPassesStorage.cleanup();

    EngineShaderFamilies::cleanEngineShaderFamilies();
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END
