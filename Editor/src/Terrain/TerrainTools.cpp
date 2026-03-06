#include "Editor/Terrain/TerrainTools.hpp"

#include "Editor/Notification.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Components/TerrainComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/Terrain/TerrainAssetIO.hpp"

#include "Core/Logger.hpp"

#include <imgui.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace
{
    constexpr float kEpsilon = 1e-6f;

    bool buildWorldRayFromNdc(const glm::vec2 &ndcPosition,
                              const elix::engine::Camera *camera,
                              glm::vec3 &outOrigin,
                              glm::vec3 &outDirection)
    {
        if (!camera)
            return false;

        const glm::mat4 view = camera->getViewMatrix();
        const glm::mat4 projection = camera->getProjectionMatrix();
        const glm::mat4 inverseViewProjection = glm::inverse(projection * view);

        glm::vec4 nearPoint = inverseViewProjection * glm::vec4(ndcPosition.x, ndcPosition.y, -1.0f, 1.0f);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(ndcPosition.x, ndcPosition.y, 1.0f, 1.0f);

        if (std::abs(nearPoint.w) <= kEpsilon || std::abs(farPoint.w) <= kEpsilon)
            return false;

        nearPoint /= nearPoint.w;
        farPoint /= farPoint.w;

        const glm::vec3 direction = glm::vec3(farPoint - nearPoint);
        const float directionLength = glm::length(direction);
        if (directionLength <= kEpsilon)
            return false;

        outOrigin = glm::vec3(nearPoint);
        outDirection = direction / directionLength;
        return true;
    }

    bool intersectRayAabb(const glm::vec3 &rayOrigin,
                          const glm::vec3 &rayDirection,
                          const glm::vec3 &aabbMin,
                          const glm::vec3 &aabbMax,
                          float &outEnterT,
                          float &outExitT)
    {
        float tMin = -std::numeric_limits<float>::infinity();
        float tMax = std::numeric_limits<float>::infinity();

        for (int axis = 0; axis < 3; ++axis)
        {
            const float origin = rayOrigin[axis];
            const float direction = rayDirection[axis];
            const float minV = aabbMin[axis];
            const float maxV = aabbMax[axis];

            if (std::abs(direction) <= kEpsilon)
            {
                if (origin < minV || origin > maxV)
                    return false;
                continue;
            }

            float t1 = (minV - origin) / direction;
            float t2 = (maxV - origin) / direction;
            if (t1 > t2)
                std::swap(t1, t2);

            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);

            if (tMin > tMax)
                return false;
        }

        outEnterT = tMin;
        outExitT = tMax;
        return tMax >= 0.0f;
    }

    float sampleTerrainHeightBilinear(const elix::engine::TerrainAsset &terrainAsset, float localX, float localZ)
    {
        if (!terrainAsset.isValid())
            return 0.0f;

        const float halfWorldX = terrainAsset.worldSizeX * 0.5f;
        const float halfWorldZ = terrainAsset.worldSizeZ * 0.5f;
        const float safeWorldSizeX = std::max(terrainAsset.worldSizeX, kEpsilon);
        const float safeWorldSizeZ = std::max(terrainAsset.worldSizeZ, kEpsilon);

        const float u = std::clamp((localX + halfWorldX) / safeWorldSizeX, 0.0f, 1.0f);
        const float v = std::clamp((localZ + halfWorldZ) / safeWorldSizeZ, 0.0f, 1.0f);

        const float gridX = u * static_cast<float>(terrainAsset.width - 1u);
        const float gridY = v * static_cast<float>(terrainAsset.height - 1u);

        const uint32_t x0 = static_cast<uint32_t>(std::floor(gridX));
        const uint32_t y0 = static_cast<uint32_t>(std::floor(gridY));
        const uint32_t x1 = std::min(x0 + 1u, terrainAsset.width - 1u);
        const uint32_t y1 = std::min(y0 + 1u, terrainAsset.height - 1u);

        const float tx = std::clamp(gridX - static_cast<float>(x0), 0.0f, 1.0f);
        const float ty = std::clamp(gridY - static_cast<float>(y0), 0.0f, 1.0f);

        const float h00 = terrainAsset.sampleWorldHeight(x0, y0);
        const float h10 = terrainAsset.sampleWorldHeight(x1, y0);
        const float h01 = terrainAsset.sampleWorldHeight(x0, y1);
        const float h11 = terrainAsset.sampleWorldHeight(x1, y1);

        const float h0 = glm::mix(h00, h10, tx);
        const float h1 = glm::mix(h01, h11, tx);
        return glm::mix(h0, h1, ty);
    }

    bool intersectRayTerrainSurface(const elix::engine::TerrainAsset &terrainAsset,
                                    const glm::vec3 &rayOriginLocal,
                                    const glm::vec3 &rayDirectionLocal,
                                    glm::vec3 &outHitLocal)
    {
        if (!terrainAsset.isValid())
            return false;

        const glm::vec3 aabbMin(-terrainAsset.worldSizeX * 0.5f, 0.0f, -terrainAsset.worldSizeZ * 0.5f);
        const glm::vec3 aabbMax(terrainAsset.worldSizeX * 0.5f, terrainAsset.heightScale, terrainAsset.worldSizeZ * 0.5f);

        float tEnter = 0.0f;
        float tExit = 0.0f;
        if (!intersectRayAabb(rayOriginLocal, rayDirectionLocal, aabbMin, aabbMax, tEnter, tExit))
            return false;

        const float startT = std::max(0.0f, tEnter);
        const float endT = std::max(startT, tExit);

        const float gridStepX = terrainAsset.worldSizeX / static_cast<float>(std::max(terrainAsset.width - 1u, 1u));
        const float gridStepZ = terrainAsset.worldSizeZ / static_cast<float>(std::max(terrainAsset.height - 1u, 1u));
        const float stepDistance = std::max(0.05f, std::min(gridStepX, gridStepZ) * 0.5f);

        const int maxSteps = std::clamp(static_cast<int>((endT - startT) / stepDistance) + 2, 16, 4096);

        float previousT = startT;
        glm::vec3 previousPoint = rayOriginLocal + rayDirectionLocal * previousT;
        float previousDiff = previousPoint.y - sampleTerrainHeightBilinear(terrainAsset, previousPoint.x, previousPoint.z);

        for (int stepIndex = 1; stepIndex <= maxSteps; ++stepIndex)
        {
            const float currentT = std::min(endT, startT + static_cast<float>(stepIndex) * stepDistance);
            const glm::vec3 currentPoint = rayOriginLocal + rayDirectionLocal * currentT;
            const float currentDiff = currentPoint.y - sampleTerrainHeightBilinear(terrainAsset, currentPoint.x, currentPoint.z);

            const bool crossedSurface = (previousDiff >= 0.0f && currentDiff <= 0.0f) ||
                                        (previousDiff <= 0.0f && currentDiff >= 0.0f);

            if (crossedSurface)
            {
                float lowT = previousT;
                float highT = currentT;
                float lowDiff = previousDiff;

                for (int refineStep = 0; refineStep < 10; ++refineStep)
                {
                    const float midT = (lowT + highT) * 0.5f;
                    const glm::vec3 midPoint = rayOriginLocal + rayDirectionLocal * midT;
                    const float midDiff = midPoint.y - sampleTerrainHeightBilinear(terrainAsset, midPoint.x, midPoint.z);

                    const bool sameSign = (lowDiff >= 0.0f && midDiff >= 0.0f) || (lowDiff <= 0.0f && midDiff <= 0.0f);
                    if (sameSign)
                    {
                        lowT = midT;
                        lowDiff = midDiff;
                    }
                    else
                        highT = midT;
                }

                const float hitT = (lowT + highT) * 0.5f;
                outHitLocal = rayOriginLocal + rayDirectionLocal * hitT;
                return true;
            }

            previousT = currentT;
            previousPoint = currentPoint;
            previousDiff = currentDiff;
        }

        return false;
    }

    float computeBrushWeight(float normalizedDistance, float falloff, bool useWorldSpaceFalloff)
    {
        const float t = 1.0f - std::clamp(normalizedDistance, 0.0f, 1.0f);
        if (t <= 0.0f)
            return 0.0f;

        if (!useWorldSpaceFalloff)
            return t;

        const float exponent = glm::mix(4.0f, 0.35f, std::clamp(falloff, 0.0f, 1.0f));
        return std::pow(t, exponent);
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(editor)

void TerrainTools::setScene(const engine::Scene::SharedPtr &scene)
{
    m_scene = scene;
}

void TerrainTools::setProjectRootPath(const std::filesystem::path &projectRootPath)
{
    m_projectRootPath = projectRootPath.lexically_normal();
}

const TerrainTools::BrushSettings &TerrainTools::getBrushSettings() const
{
    return m_brushSettings;
}

bool TerrainTools::draw(bool *open, NotificationManager *notifications)
{
    bool executedOperation = false;

    if (open && !*open)
        return false;

    if (!ImGui::Begin("Terrain Tools", open))
    {
        ImGui::End();
        return false;
    }

    ImGui::SeparatorText("Asset");
    ImGui::InputText("Terrain Name", m_newTerrainName, sizeof(m_newTerrainName));
    ImGui::InputInt("Resolution", &m_newTerrainResolution);
    ImGui::DragFloat("World Size X", &m_newTerrainWorldSizeX, 1.0f, 1.0f, 100000.0f, "%.1f m");
    ImGui::DragFloat("World Size Z", &m_newTerrainWorldSizeZ, 1.0f, 1.0f, 100000.0f, "%.1f m");
    ImGui::DragFloat("Height Scale", &m_newTerrainHeightScale, 0.5f, 0.01f, 10000.0f, "%.2f m");
    ImGui::InputInt("Chunk Quads", &m_newTerrainChunkQuads);

    if (ImGui::Button("Create Flat Terrain Asset"))
    {
        std::string createdAssetPath;
        std::string error;
        if (createFlatTerrainAsset(createdAssetPath, error))
        {
            std::strncpy(m_spawnTerrainAssetPath, createdAssetPath.c_str(), sizeof(m_spawnTerrainAssetPath) - 1u);
            m_spawnTerrainAssetPath[sizeof(m_spawnTerrainAssetPath) - 1u] = '\0';

            if (notifications)
                notifications->showSuccess("Terrain asset created");

            VX_EDITOR_INFO_STREAM("Created terrain asset: " << createdAssetPath << '\n');
            executedOperation = true;
        }
        else
        {
            if (notifications)
                notifications->showError(error.empty() ? "Failed to create terrain asset" : error);

            VX_EDITOR_ERROR_STREAM("Terrain asset creation failed: " << error << '\n');
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Spawn");
    ImGui::InputTextWithHint("Terrain Asset Path", "Path to .elixterrain", m_spawnTerrainAssetPath, sizeof(m_spawnTerrainAssetPath));
    ImGui::InputTextWithHint("Material Override Path", "Optional .elixmat", m_spawnTerrainMaterialPath, sizeof(m_spawnTerrainMaterialPath));

    if (ImGui::Button("Spawn Terrain Entity"))
    {
        std::string error;
        if (spawnTerrainEntityFromAsset(m_spawnTerrainAssetPath, error))
        {
            if (notifications)
                notifications->showSuccess("Terrain entity added to scene");
            executedOperation = true;
        }
        else
        {
            if (notifications)
                notifications->showError(error.empty() ? "Failed to spawn terrain entity" : error);
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Brush");
    static const char *kBrushModes[] = {
        "Raise",
        "Lower",
        "Smooth",
        "Flatten",
        "Paint Layer",
    };

    int brushMode = static_cast<int>(m_brushSettings.mode);
    if (ImGui::Combo("Mode", &brushMode, kBrushModes, IM_ARRAYSIZE(kBrushModes)))
        m_brushSettings.mode = static_cast<BrushMode>(std::clamp(brushMode, 0, static_cast<int>(BrushMode::PaintLayer)));

    ImGui::DragFloat("Radius", &m_brushSettings.radius, 0.05f, 0.01f, 512.0f, "%.2f");
    ImGui::SliderFloat("Strength", &m_brushSettings.strength, 0.0f, 1.0f, "%.3f");
    ImGui::SliderFloat("Falloff", &m_brushSettings.falloff, 0.0f, 1.0f, "%.3f");
    ImGui::Checkbox("World-space falloff", &m_brushSettings.useWorldSpaceFalloff);

    if (m_brushSettings.mode == BrushMode::PaintLayer)
    {
        ImGui::InputInt("Paint Layer", &m_brushPaintLayerIndex);
        m_brushPaintLayerIndex = std::max(0, m_brushPaintLayerIndex);
        ImGui::Checkbox("Normalize painted weights", &m_brushAutoNormalizeWeights);
        ImGui::TextDisabled("Note: layer weight painting data is stored in .elixterrain.");
    }

    ImGui::TextDisabled("Usage: select a terrain entity, hold left mouse in Viewport and drag.");

    ImGui::End();
    return executedOperation;
}

bool TerrainTools::applyBrushStrokeFromNdc(const glm::vec2 &ndcPosition,
                                           const engine::Camera *camera,
                                           engine::Entity *selectedEntity,
                                           float deltaTime,
                                           bool strokeStart)
{
    if (!m_scene || !camera)
        return false;

    engine::Entity *targetEntity = selectedEntity;
    auto *terrainComponent = targetEntity ? targetEntity->getComponent<engine::TerrainComponent>() : nullptr;
    if (!terrainComponent)
    {
        for (const auto &entity : m_scene->getEntities())
        {
            if (!entity || !entity->isEnabled())
                continue;

            auto *candidateTerrain = entity->getComponent<engine::TerrainComponent>();
            if (!candidateTerrain)
                continue;

            targetEntity = entity.get();
            terrainComponent = candidateTerrain;
            break;
        }
    }

    if (!terrainComponent)
        return false;

    auto terrainAssetShared = terrainComponent->getTerrainAsset();
    if (!terrainAssetShared || !terrainAssetShared->isValid())
        return false;

    if (strokeStart || !m_brushStrokeActive)
    {
        m_brushStrokeActive = true;
        m_hasFlattenTarget = false;
    }

    glm::vec3 rayOriginWorld(0.0f);
    glm::vec3 rayDirectionWorld(0.0f, -1.0f, 0.0f);
    if (!buildWorldRayFromNdc(ndcPosition, camera, rayOriginWorld, rayDirectionWorld))
        return false;

    glm::mat4 terrainWorldTransform(1.0f);
    if (auto *transformComponent = targetEntity->getComponent<engine::Transform3DComponent>())
        terrainWorldTransform = transformComponent->getMatrix();

    const glm::mat4 inverseTerrainTransform = glm::inverse(terrainWorldTransform);
    const glm::vec3 rayOriginLocal = glm::vec3(inverseTerrainTransform * glm::vec4(rayOriginWorld, 1.0f));

    glm::vec3 rayDirectionLocal = glm::vec3(inverseTerrainTransform * glm::vec4(rayDirectionWorld, 0.0f));
    const float localDirectionLength = glm::length(rayDirectionLocal);
    if (localDirectionLength <= kEpsilon)
        return false;
    rayDirectionLocal /= localDirectionLength;

    glm::vec3 hitLocal(0.0f);
    if (!intersectRayTerrainSurface(*terrainAssetShared, rayOriginLocal, rayDirectionLocal, hitLocal))
        return false;

    engine::TerrainAsset &terrainAsset = *terrainAssetShared;
    const float brushRadius = std::max(0.01f, m_brushSettings.radius);
    const float dt = (deltaTime > 0.0f) ? deltaTime : (1.0f / 60.0f);
    const float brushStrength = std::clamp(m_brushSettings.strength, 0.0f, 1.0f);

    const float halfWorldX = terrainAsset.worldSizeX * 0.5f;
    const float halfWorldZ = terrainAsset.worldSizeZ * 0.5f;
    const float gridStepX = terrainAsset.worldSizeX / static_cast<float>(std::max(terrainAsset.width - 1u, 1u));
    const float gridStepZ = terrainAsset.worldSizeZ / static_cast<float>(std::max(terrainAsset.height - 1u, 1u));

    const float centerU = std::clamp((hitLocal.x + halfWorldX) / std::max(terrainAsset.worldSizeX, kEpsilon), 0.0f, 1.0f);
    const float centerV = std::clamp((hitLocal.z + halfWorldZ) / std::max(terrainAsset.worldSizeZ, kEpsilon), 0.0f, 1.0f);
    const float centerGridX = centerU * static_cast<float>(terrainAsset.width - 1u);
    const float centerGridY = centerV * static_cast<float>(terrainAsset.height - 1u);

    const int sampleRadiusX = std::max(1, static_cast<int>(std::ceil(brushRadius / std::max(gridStepX, kEpsilon))));
    const int sampleRadiusY = std::max(1, static_cast<int>(std::ceil(brushRadius / std::max(gridStepZ, kEpsilon))));

    const int centerSampleX = static_cast<int>(std::round(centerGridX));
    const int centerSampleY = static_cast<int>(std::round(centerGridY));

    const int sampleMinX = std::clamp(centerSampleX - sampleRadiusX, 0, static_cast<int>(terrainAsset.width) - 1);
    const int sampleMaxX = std::clamp(centerSampleX + sampleRadiusX, 0, static_cast<int>(terrainAsset.width) - 1);
    const int sampleMinY = std::clamp(centerSampleY - sampleRadiusY, 0, static_cast<int>(terrainAsset.height) - 1);
    const int sampleMaxY = std::clamp(centerSampleY + sampleRadiusY, 0, static_cast<int>(terrainAsset.height) - 1);

    if (m_brushSettings.mode == BrushMode::Flatten && !m_hasFlattenTarget)
    {
        m_flattenTargetWorldHeight = sampleTerrainHeightBilinear(terrainAsset, hitLocal.x, hitLocal.z);
        m_hasFlattenTarget = true;
    }

    std::vector<uint16_t> smoothSourceHeights;
    if (m_brushSettings.mode == BrushMode::Smooth)
        smoothSourceHeights = terrainAsset.heightSamples;

    if (m_brushSettings.mode == BrushMode::PaintLayer && terrainAsset.weightmapData.empty())
    {
        terrainAsset.weightmapWidth = terrainAsset.width;
        terrainAsset.weightmapHeight = terrainAsset.height;
        terrainAsset.weightmapChannels = std::clamp<uint32_t>(std::max<uint32_t>(1u, static_cast<uint32_t>(terrainAsset.layers.size())), 1u, 4u);
        terrainAsset.weightmapData.assign(static_cast<size_t>(terrainAsset.weightmapWidth) *
                                              static_cast<size_t>(terrainAsset.weightmapHeight) *
                                              static_cast<size_t>(terrainAsset.weightmapChannels),
                                          0u);

        for (size_t i = 0; i < terrainAsset.weightmapData.size(); i += terrainAsset.weightmapChannels)
            terrainAsset.weightmapData[i] = 255u;
    }

    bool heightDataChanged = false;

    for (int sampleY = sampleMinY; sampleY <= sampleMaxY; ++sampleY)
    {
        for (int sampleX = sampleMinX; sampleX <= sampleMaxX; ++sampleX)
        {
            const float sampleWorldX = -halfWorldX + static_cast<float>(sampleX) * gridStepX;
            const float sampleWorldZ = -halfWorldZ + static_cast<float>(sampleY) * gridStepZ;

            const float distance = glm::length(glm::vec2(sampleWorldX - hitLocal.x, sampleWorldZ - hitLocal.z));
            if (distance > brushRadius)
                continue;

            const float normalizedDistance = distance / brushRadius;
            const float brushWeight = computeBrushWeight(normalizedDistance, m_brushSettings.falloff, m_brushSettings.useWorldSpaceFalloff);
            const float weightedStrength = brushStrength * brushWeight;
            if (weightedStrength <= 0.0f)
                continue;

            if (m_brushSettings.mode == BrushMode::PaintLayer)
            {
                if (terrainAsset.weightmapWidth == 0u || terrainAsset.weightmapHeight == 0u || terrainAsset.weightmapChannels == 0u)
                    continue;

                const float paintRate = weightedStrength * dt;
                const float weightXf = (static_cast<float>(sampleX) / static_cast<float>(std::max(terrainAsset.width - 1u, 1u))) *
                                       static_cast<float>(std::max(terrainAsset.weightmapWidth - 1u, 1u));
                const float weightYf = (static_cast<float>(sampleY) / static_cast<float>(std::max(terrainAsset.height - 1u, 1u))) *
                                       static_cast<float>(std::max(terrainAsset.weightmapHeight - 1u, 1u));

                const uint32_t weightX = static_cast<uint32_t>(std::round(weightXf));
                const uint32_t weightY = static_cast<uint32_t>(std::round(weightYf));
                const size_t baseIndex = (static_cast<size_t>(weightY) * static_cast<size_t>(terrainAsset.weightmapWidth) +
                                          static_cast<size_t>(weightX)) *
                                         static_cast<size_t>(terrainAsset.weightmapChannels);

                if (baseIndex + terrainAsset.weightmapChannels > terrainAsset.weightmapData.size())
                    continue;

                std::array<float, 4> channels{0.0f, 0.0f, 0.0f, 0.0f};
                for (uint32_t channelIndex = 0; channelIndex < terrainAsset.weightmapChannels; ++channelIndex)
                    channels[channelIndex] = static_cast<float>(terrainAsset.weightmapData[baseIndex + channelIndex]);

                const int activeLayer = std::clamp(m_brushPaintLayerIndex, 0, static_cast<int>(terrainAsset.weightmapChannels) - 1);
                const float paintDelta = paintRate * 255.0f * 8.0f;
                channels[activeLayer] = std::clamp(channels[activeLayer] + paintDelta, 0.0f, 255.0f);

                if (terrainAsset.weightmapChannels > 1u)
                {
                    const float bleed = paintDelta / static_cast<float>(terrainAsset.weightmapChannels - 1u);
                    for (uint32_t channelIndex = 0; channelIndex < terrainAsset.weightmapChannels; ++channelIndex)
                    {
                        if (static_cast<int>(channelIndex) == activeLayer)
                            continue;
                        channels[channelIndex] = std::clamp(channels[channelIndex] - bleed, 0.0f, 255.0f);
                    }
                }

                if (m_brushAutoNormalizeWeights)
                {
                    float sum = 0.0f;
                    for (uint32_t channelIndex = 0; channelIndex < terrainAsset.weightmapChannels; ++channelIndex)
                        sum += channels[channelIndex];

                    if (sum > kEpsilon)
                    {
                        const float scale = 255.0f / sum;
                        for (uint32_t channelIndex = 0; channelIndex < terrainAsset.weightmapChannels; ++channelIndex)
                            channels[channelIndex] = std::clamp(channels[channelIndex] * scale, 0.0f, 255.0f);
                    }
                    else
                    {
                        for (uint32_t channelIndex = 0; channelIndex < terrainAsset.weightmapChannels; ++channelIndex)
                            channels[channelIndex] = 0.0f;
                        channels[activeLayer] = 255.0f;
                    }
                }

                for (uint32_t channelIndex = 0; channelIndex < terrainAsset.weightmapChannels; ++channelIndex)
                {
                    const float roundedChannelValue = std::round(channels[channelIndex]);
                    const uint8_t channelValue = static_cast<uint8_t>(std::clamp(roundedChannelValue, 0.0f, 255.0f));
                    if (terrainAsset.weightmapData[baseIndex + channelIndex] != channelValue)
                    {
                        terrainAsset.weightmapData[baseIndex + channelIndex] = channelValue;
                    }
                }

                continue;
            }

            const size_t sampleIndex = static_cast<size_t>(sampleY) * static_cast<size_t>(terrainAsset.width) + static_cast<size_t>(sampleX);
            if (sampleIndex >= terrainAsset.heightSamples.size())
                continue;

            const float currentHeightNormalized = static_cast<float>(terrainAsset.heightSamples[sampleIndex]) / 65535.0f;
            float newHeightNormalized = currentHeightNormalized;

            const float sculptRate = weightedStrength * dt;
            switch (m_brushSettings.mode)
            {
            case BrushMode::Raise:
                newHeightNormalized += sculptRate * 6.0f;
                break;

            case BrushMode::Lower:
                newHeightNormalized -= sculptRate * 6.0f;
                break;

            case BrushMode::Smooth:
            {
                float neighborSum = 0.0f;
                int neighborCount = 0;
                for (int offsetY = -1; offsetY <= 1; ++offsetY)
                {
                    for (int offsetX = -1; offsetX <= 1; ++offsetX)
                    {
                        const int neighborX = std::clamp(sampleX + offsetX, 0, static_cast<int>(terrainAsset.width) - 1);
                        const int neighborY = std::clamp(sampleY + offsetY, 0, static_cast<int>(terrainAsset.height) - 1);
                        const size_t neighborIndex = static_cast<size_t>(neighborY) * static_cast<size_t>(terrainAsset.width) + static_cast<size_t>(neighborX);
                        if (neighborIndex >= smoothSourceHeights.size())
                            continue;

                        neighborSum += static_cast<float>(smoothSourceHeights[neighborIndex]) / 65535.0f;
                        ++neighborCount;
                    }
                }

                if (neighborCount > 0)
                {
                    const float averagedHeight = neighborSum / static_cast<float>(neighborCount);
                    const float blend = std::clamp(sculptRate * 18.0f, 0.0f, 1.0f);
                    newHeightNormalized = glm::mix(currentHeightNormalized, averagedHeight, blend);
                }
                break;
            }

            case BrushMode::Flatten:
            {
                const float targetHeightNormalized = std::clamp(
                    m_flattenTargetWorldHeight / std::max(terrainAsset.heightScale, 0.01f),
                    0.0f,
                    1.0f);
                const float blend = std::clamp(sculptRate * 20.0f, 0.0f, 1.0f);
                newHeightNormalized = glm::mix(currentHeightNormalized, targetHeightNormalized, blend);
                break;
            }

            case BrushMode::PaintLayer:
                break;
            }

            newHeightNormalized = std::clamp(newHeightNormalized, 0.0f, 1.0f);
            const float roundedHeightSample = std::round(newHeightNormalized * 65535.0f);
            const uint16_t newHeightSample = static_cast<uint16_t>(std::clamp(roundedHeightSample, 0.0f, 65535.0f));

            if (terrainAsset.heightSamples[sampleIndex] != newHeightSample)
            {
                terrainAsset.heightSamples[sampleIndex] = newHeightSample;
                heightDataChanged = true;
            }
        }
    }

    if (heightDataChanged)
        terrainComponent->setChunksDirty();

    return true;
}

void TerrainTools::cancelBrushStroke()
{
    m_brushStrokeActive = false;
    m_hasFlattenTarget = false;
}

bool TerrainTools::createFlatTerrainAsset(std::string &outAssetPath, std::string &outError) const
{
    outAssetPath.clear();
    outError.clear();

    if (m_projectRootPath.empty())
    {
        outError = "Project root is not set";
        return false;
    }

    std::string terrainName = m_newTerrainName;
    if (terrainName.empty())
        terrainName = "NewTerrain";

    const int clampedResolution = std::clamp(m_newTerrainResolution, 2, 4097);

    engine::TerrainAsset terrainAsset{};
    terrainAsset.name = terrainName;
    terrainAsset.width = static_cast<uint32_t>(clampedResolution);
    terrainAsset.height = static_cast<uint32_t>(clampedResolution);
    terrainAsset.worldSizeX = std::max(m_newTerrainWorldSizeX, 1.0f);
    terrainAsset.worldSizeZ = std::max(m_newTerrainWorldSizeZ, 1.0f);
    terrainAsset.heightScale = std::max(m_newTerrainHeightScale, 0.01f);
    terrainAsset.heightSamples.assign(
        static_cast<size_t>(terrainAsset.width) * static_cast<size_t>(terrainAsset.height),
        0u);

    engine::TerrainLayerInfo baseLayer{};
    baseLayer.name = "Base";
    terrainAsset.layers.push_back(std::move(baseLayer));

    std::filesystem::path outputPath = m_projectRootPath / "resources" / "terrains" / (terrainName + ".elixterrain");
    outputPath = outputPath.lexically_normal();

    if (!engine::saveTerrainAssetToFile(terrainAsset, outputPath.string()))
    {
        outError = "Could not write terrain asset file";
        return false;
    }

    outAssetPath = outputPath.string();
    return true;
}

bool TerrainTools::spawnTerrainEntityFromAsset(const std::string &assetPath, std::string &outError)
{
    outError.clear();

    if (!m_scene)
    {
        outError = "No active scene";
        return false;
    }

    if (assetPath.empty())
    {
        outError = "Terrain asset path is empty";
        return false;
    }

    std::filesystem::path resolvedPath(assetPath);
    if (resolvedPath.is_relative() && !m_projectRootPath.empty())
        resolvedPath = (m_projectRootPath / resolvedPath).lexically_normal();

    auto terrainAsset = engine::AssetsLoader::loadTerrain(resolvedPath.string());
    if (!terrainAsset.has_value())
    {
        outError = "Could not load terrain asset";
        return false;
    }

    const std::string entityName = std::filesystem::path(assetPath).stem().string().empty()
                                       ? std::string("Terrain")
                                       : std::filesystem::path(assetPath).stem().string();

    auto entity = m_scene->addEntity(entityName);
    if (!entity)
    {
        outError = "Failed to create terrain entity";
        return false;
    }

    auto *terrainComponent = entity->addComponent<engine::TerrainComponent>();
    terrainComponent->setTerrainAssetPath(resolvedPath.string());
    terrainComponent->setQuadsPerChunk(static_cast<uint32_t>(std::clamp(m_newTerrainChunkQuads, 1, 512)));
    terrainComponent->setTerrainAsset(std::make_shared<engine::TerrainAsset>(std::move(terrainAsset.value())));

    if (m_spawnTerrainMaterialPath[0] != '\0')
    {
        std::filesystem::path materialPath(m_spawnTerrainMaterialPath);
        if (materialPath.is_relative() && !m_projectRootPath.empty())
            materialPath = (m_projectRootPath / materialPath).lexically_normal();

        terrainComponent->setMaterialOverridePath(materialPath.string());
    }

    VX_EDITOR_INFO_STREAM("Spawned terrain entity from asset: " << resolvedPath << '\n');
    return true;
}

ELIX_NESTED_NAMESPACE_END
