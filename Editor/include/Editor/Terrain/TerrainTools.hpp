#ifndef ELIX_EDITOR_TERRAIN_TOOLS_HPP
#define ELIX_EDITOR_TERRAIN_TOOLS_HPP

#include "Core/Macros.hpp"
#include "Engine/Camera.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Scene.hpp"

#include <glm/vec2.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class NotificationManager;

class TerrainTools
{
public:
    enum class BrushMode : uint8_t
    {
        Raise = 0,
        Lower = 1,
        Smooth = 2,
        Flatten = 3,
        PaintLayer = 4
    };

    struct BrushSettings
    {
        BrushMode mode{BrushMode::Raise};
        float radius{4.0f};
        float strength{0.35f};
        float falloff{0.75f};
        bool useWorldSpaceFalloff{true};
    };

    void setScene(const engine::Scene::SharedPtr &scene);
    void setProjectRootPath(const std::filesystem::path &projectRootPath);

    // Returns true when any terrain operation was executed this frame.
    bool draw(bool *open, NotificationManager *notifications);

    // Returns true when the brush consumed input and modified terrain data.
    bool applyBrushStrokeFromNdc(const glm::vec2 &ndcPosition,
                                 const engine::Camera *camera,
                                 engine::Entity *selectedEntity,
                                 float deltaTime,
                                 bool strokeStart);

    void cancelBrushStroke();

    const BrushSettings &getBrushSettings() const;

private:
    bool createFlatTerrainAsset(std::string &outAssetPath, std::string &outError) const;
    bool spawnTerrainEntityFromAsset(const std::string &assetPath, std::string &outError);

private:
    engine::Scene::SharedPtr m_scene{nullptr};
    std::filesystem::path m_projectRootPath{};

    BrushSettings m_brushSettings{};

    char m_newTerrainName[128]{"NewTerrain"};
    int m_newTerrainResolution{257};
    float m_newTerrainWorldSizeX{512.0f};
    float m_newTerrainWorldSizeZ{512.0f};
    float m_newTerrainHeightScale{80.0f};
    int m_newTerrainChunkQuads{63};

    char m_spawnTerrainAssetPath[512]{};
    char m_spawnTerrainMaterialPath[512]{};

    int m_brushPaintLayerIndex{0};
    bool m_brushAutoNormalizeWeights{true};
    bool m_brushStrokeActive{false};
    bool m_hasFlattenTarget{false};
    float m_flattenTargetWorldHeight{0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EDITOR_TERRAIN_TOOLS_HPP
