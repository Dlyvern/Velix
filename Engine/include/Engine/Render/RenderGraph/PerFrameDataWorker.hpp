#ifndef ELIX_PER_FRAME_DATA_WORKER_HPP
#define ELIX_PER_FRAME_DATA_WORKER_HPP

#include "Core/Macros.hpp"

#include "Engine/Render/RenderGraphPassPerFrameData.hpp"
#include "Engine/Camera.hpp"
#include "Engine/Components/LightComponent.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)
class Material;
class MeshGeometryRegistry;
class SceneMaterialResolver;
class BindlessRegistry;
class Scene;
class StaticMeshComponent;
class SkeletalMeshComponent;
class TerrainComponent;

namespace rayTracing
{
    class RayTracingGeometryCache;
    class RayTracingScene;
    class SkinnedBlasBuilder;
}

ELIX_CUSTOM_NAMESPACE_BEGIN(renderGraph)

class PerFrameDataWorker
{
public:
    struct Dependencies
    {
        MeshGeometryRegistry *meshGeometryRegistry{nullptr};
        SceneMaterialResolver *materialResolver{nullptr};
        BindlessRegistry *bindlessRegistry{nullptr};
        rayTracing::RayTracingScene *rayTracingScene{nullptr};
        rayTracing::RayTracingGeometryCache *rayTracingGeometryCache{nullptr};
        rayTracing::SkinnedBlasBuilder *skinnedBlasBuilder{nullptr};
        std::array<glm::vec4, 6> *lastFrustumPlanes{nullptr};
        bool *lastFrustumCullingEnabled{nullptr};
        uint32_t currentFrame{0u};
        bool enableCpuSmallFeatureCulling{true};
    };

    static PerFrameDataWorker begin(RenderGraphPassPerFrameData &data, Dependencies dependencies);

    void fillCameraData(Camera *camera);

    void buildLightData(Scene *scene, Camera *camera);
    bool fillPointLight(Camera *camera, PointLight *pointLight);
    bool fillSpotLight(Camera *camera, SpotLight *spotLight);
    bool fillDirectionalLight(Camera *camera, DirectionalLight *directionalLight);

    void pruneRemovedEntities(Scene *scene);
    void syncSceneDrawItems(Scene *scene, const glm::vec3 &cameraPos = glm::vec3(0.0f));
    void buildFrameBones();
    void buildDrawReferences(const glm::mat4 &view, const glm::mat4 &projection, bool enableFrustumCulling);
    void sortDrawReferences(const glm::vec3 &cameraPosition);
    void buildRayTracingInputs();
    void buildRasterBatches();
    void buildShadowBatches();

    const std::vector<glm::mat4> &getFrameBones() const;
    const std::vector<PerObjectInstanceData> &getShadowPerObjectInstances() const;
    const std::vector<RenderGraphLightData> &getLightData() const;
    const RenderGraphLightSpaceMatrixUBO &getLightSpaceMatrixUBO() const;

    void resetPerFrameData();

private:
    struct MeshDrawReference
    {
        Entity *entity{nullptr};
        DrawItem *drawItem{nullptr};
        DrawItem::DrawMeshState *meshState{nullptr};
        uint32_t meshIndex{0u};
        GPUMesh::SharedPtr mesh{nullptr};
        Material::SharedPtr material{nullptr};
        glm::vec3 worldBoundsCenter{0.0f};
        float worldBoundsRadius{0.0f};
        bool skinned{false};
        glm::mat4 modelMatrix{1.0f};
    };

    PerFrameDataWorker(RenderGraphPassPerFrameData &data, Dependencies dependencies);

    void updateDrawItemBones(DrawItem &drawItem, Entity::SharedPtr entity);
    void resizeDrawMeshStates(DrawItem &drawItem, size_t meshCount);
    glm::mat4 computeMeshLocalTransform(const CPUMesh &mesh, SkeletalMeshComponent *skeletalMeshComponent) const;
    Material::SharedPtr resolveMeshMaterial(const CPUMesh &mesh,
                                            StaticMeshComponent *staticComponent,
                                            SkeletalMeshComponent *skeletalComponent,
                                            TerrainComponent *terrainComponent,
                                            size_t slot);
    void updateWorldBounds(DrawItem &drawItem);
    static bool hasSameGeometry(const GPUMesh::SharedPtr &left, const GPUMesh::SharedPtr &right);
    static bool isTranslucentReference(const MeshDrawReference &reference);
    bool hasSameShadowKey(const DrawBatch &batch, const MeshDrawReference &reference) const;
    float shadowTexelWorldSize(const glm::mat4 &vp) const;
    void buildShadowBatchesForTarget(std::vector<DrawBatch> &outBatches,
                                     const std::array<glm::vec4, 6> *cullPlanes,
                                     float minMeshRadius);

private:
    RenderGraphPassPerFrameData &m_data;
    Dependencies m_dependencies;
    std::vector<MeshDrawReference> m_drawReferences;
    std::vector<const MeshDrawReference *> m_shadowReferences;
    std::vector<glm::mat4> m_frameBones;
    std::vector<PerObjectInstanceData> m_shadowPerObjectInstances;
    std::vector<RenderGraphLightData> m_lightData;
    RenderGraphLightSpaceMatrixUBO m_lightSpaceMatrixUBO{};

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
    const uint64_t m_skinnedVertexLayoutHash{vertex::VertexTraits<vertex::VertexSkinned>::layout().hash};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PER_FRAME_DATA_WORKER_HPP
