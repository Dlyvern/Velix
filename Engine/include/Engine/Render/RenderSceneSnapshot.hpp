#ifndef ELIX_RENDER_SCENE_SNAPSHOT_HPP
#define ELIX_RENDER_SCENE_SNAPSHOT_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/EnvironmentSettings.hpp"
#include "Engine/Lights.hpp"
#include "Engine/Mesh.hpp"
#include "Engine/Components/LightComponent.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class StaticMeshComponent;
class SkeletalMeshComponent;
class TerrainComponent;


enum class SnapshotMeshSource : uint8_t
{
    None,
    Static,
    Skeletal,
    Terrain
};




struct RenderEntitySnapshot
{
    Entity::SharedPtr entityRef;
    Entity *entityPtr{nullptr};
    uint32_t entityId{0};
    bool enabled{true};

    glm::mat4 worldTransform{1.0f};

    SnapshotMeshSource meshSource{SnapshotMeshSource::None};
    const std::vector<CPUMesh> *meshes{nullptr};
    bool meshVisible{false};
    bool meshReady{false};




    StaticMeshComponent *staticMeshComponent{nullptr};
    SkeletalMeshComponent *skeletalMeshComponent{nullptr};
    TerrainComponent *terrainComponent{nullptr};


    std::vector<glm::mat4> finalBones;
    bool hasSkeleton{false};
};



struct RenderLightSnapshot
{
    LightComponent::LightType type{LightComponent::LightType::NONE};

    glm::vec3 color{1.0f};
    glm::vec3 position{0.0f};
    float strength{1.0f};
    bool castsShadows{true};


    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    bool skyLightEnabled{false};


    float radius{10.0f};


    float innerAngle{15.0f};
    float outerAngle{30.0f};
    float range{10.0f};
};



struct RenderSceneSnapshot
{
    std::vector<RenderEntitySnapshot> entities;
    std::vector<RenderLightSnapshot> lights;
    std::string skyboxHDRPath;
    FogSettings fogSettings{};
    float deltaTime{0.0f};




    glm::vec3 cameraVelocity{0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif
