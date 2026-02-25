#include "Engine/Assets/FBXAssetLoader.hpp"

#include <iostream>
#include <algorithm>

#include "Engine/Skeleton.hpp"

struct VertexKey
{
    int controlPoint;
    FbxVector4 normal;
    FbxVector2 uv;

    bool operator==(const VertexKey &other) const
    {
        return controlPoint == other.controlPoint &&
               normal == other.normal &&
               uv == other.uv;
    }
};

struct Influence
{
    int boneId;
    float weight;
};

struct TmpVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 textureCoordinates;

    int controlPointIndex;
};

struct VertexKeyHash
{
    size_t operator()(const VertexKey &key) const
    {
        auto h1 = std::hash<int>()(key.controlPoint);
        auto h2 = std::hash<double>()(key.normal[0] + key.normal[1] + key.normal[2]);
        auto h3 = std::hash<double>()(key.uv[0] + key.uv[1]);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

namespace
{
bool hasAnyTransformCurve(FbxNode *node, FbxAnimLayer *layer)
{
    if (!node || !layer)
        return false;

    const bool hasTranslation = node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X) ||
                                node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y) ||
                                node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z);
    const bool hasRotation = node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X) ||
                             node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y) ||
                             node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z);
    const bool hasScale = node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X) ||
                          node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y) ||
                          node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z);

    return hasTranslation || hasRotation || hasScale;
}

std::vector<FbxTime> collectTrackKeyTimes(FbxNode *node, FbxAnimLayer *layer, const FbxTime &clipStart, const FbxTime &clipEnd)
{
    std::vector<FbxTime> keyTimes;

    auto collectCurveTimes = [&keyTimes](FbxAnimCurve *curve)
    {
        if (!curve)
            return;

        const int keyCount = curve->KeyGetCount();
        keyTimes.reserve(keyTimes.size() + static_cast<size_t>(keyCount));

        for (int keyIndex = 0; keyIndex < keyCount; ++keyIndex)
            keyTimes.push_back(curve->KeyGetTime(keyIndex));
    };

    collectCurveTimes(node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X));
    collectCurveTimes(node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y));
    collectCurveTimes(node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z));

    collectCurveTimes(node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X));
    collectCurveTimes(node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y));
    collectCurveTimes(node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z));

    collectCurveTimes(node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X));
    collectCurveTimes(node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Y));
    collectCurveTimes(node->LclScaling.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_Z));

    if (keyTimes.empty())
        return keyTimes;

    std::sort(keyTimes.begin(), keyTimes.end(), [](const FbxTime &lhs, const FbxTime &rhs)
              { return lhs.Get() < rhs.Get(); });

    keyTimes.erase(std::unique(keyTimes.begin(), keyTimes.end(), [](const FbxTime &lhs, const FbxTime &rhs)
                               { return lhs.Get() == rhs.Get(); }),
                   keyTimes.end());

    if (keyTimes.front().Get() > clipStart.Get())
        keyTimes.insert(keyTimes.begin(), clipStart);
    if (keyTimes.back().Get() < clipEnd.Get())
        keyTimes.push_back(clipEnd);

    return keyTimes;
}
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void buildSkeletonHierarchy(FbxNode *node, Skeleton &skeleton, int parentId = -1)
{
    if (!node)
        return;

    const std::string name = node->GetName();
    int boneId = skeleton.getBoneId(name);

    if (boneId != -1)
    {
        auto *bone = skeleton.getBone(boneId);
        bone->parentId = parentId;

        if (parentId != -1)
            skeleton.getBone(parentId)->children.push_back(boneId);

        parentId = boneId;
    }

    for (int i = 0; i < node->GetChildCount(); ++i)
        buildSkeletonHierarchy(node->GetChild(i), skeleton, parentId);
}

FBXAssetLoader::FBXAssetLoader()
{
    m_fbxManager = FbxManager::Create();
    m_fbxIOSettings = FbxIOSettings::Create(m_fbxManager, IOSROOT);
    m_fbxManager->SetIOSettings(m_fbxIOSettings);

    // m_fbxManager->Destroy();
}

const std::vector<std::string> FBXAssetLoader::getSupportedFormats() const
{
    return {".fbx", ".FBX"};
}

std::shared_ptr<IAsset> FBXAssetLoader::load(const std::string &filePath)
{
    FbxImporter *importer = FbxImporter::Create(m_fbxManager, "");

    ProceedingMeshData meshesData;

    if (!importer->Initialize(filePath.c_str(), -1, m_fbxManager->GetIOSettings()))
    {
        VX_ENGINE_ERROR_STREAM("Failed to initialize FBX importer: " << importer->GetStatus().GetErrorString() << std::endl);
        importer->Destroy();
        return nullptr;
    }

    FbxScene *scene = FbxScene::Create(m_fbxManager, "scene");
    importer->Import(scene);
    importer->Destroy();

    FbxGeometryConverter converter(m_fbxManager);
    converter.Triangulate(scene, true);

    FbxNode *rootNode = scene->GetRootNode();

    if (!rootNode)
    {
        VX_ENGINE_ERROR_STREAM("Root node is invalid\n");
        scene->Destroy();
        return nullptr;
    }

    processNode(rootNode, meshesData);

    if (meshesData.skeleton.has_value())
    {
        auto &skeleton = meshesData.skeleton.value();
        buildSkeletonHierarchy(scene->GetRootNode(), skeleton);
    }

    auto animations = processAnimations(scene, meshesData.skeleton);

    VX_ENGINE_INFO_STREAM("Found " << animations.size() << " animations\n");

    scene->Destroy();

    VX_ENGINE_INFO_STREAM("Parsed " << meshesData.meshes.size() << " meshes\n");

    auto modelAsset = std::make_shared<ModelAsset>(meshesData.meshes, meshesData.skeleton, animations);

    return modelAsset;
}

std::vector<Animation> FBXAssetLoader::processAnimations(FbxScene *scene, std::optional<Skeleton> &skeleton)
{
    if (!scene || !skeleton.has_value())
        return {};

    const int animationsCount = scene->GetSrcObjectCount<FbxAnimStack>();

    if (animationsCount <= 0)
        return {};

    std::vector<Animation> animations;
    animations.reserve(animationsCount);

    auto timeMode = scene->GetGlobalSettings().GetTimeMode();
    double ticksPerSecond = FbxTime::GetFrameRate(timeMode);
    if (ticksPerSecond <= 0.0)
        ticksPerSecond = 30.0;

    for (int i = 0; i < animationsCount; ++i)
    {
        FbxAnimStack *animationStack = scene->GetSrcObject<FbxAnimStack>(i);
        if (!animationStack)
            continue;

        scene->SetCurrentAnimationStack(animationStack);

        FbxTimeSpan timeSpan = animationStack->GetLocalTimeSpan();

        if (timeSpan.GetDuration().Get() <= 0)
            scene->GetGlobalSettings().GetTimelineDefaultTimeSpan(timeSpan);

        const FbxTime clipStart = timeSpan.GetStart();
        const FbxTime clipEnd = timeSpan.GetStop();

        if (clipEnd.Get() < clipStart.Get())
            continue;

        auto *animationLayer = animationStack->GetMember<FbxAnimLayer>(0);
        if (!animationLayer)
            continue;

        Animation animation;
        animation.ticksPerSecond = ticksPerSecond;
        animation.duration = std::max(timeSpan.GetDuration().GetSecondDouble() * ticksPerSecond, 0.001);

        animation.name = animationStack->GetName();

        for (size_t boneIndex = 0; boneIndex < skeleton->getBonesCount(); ++boneIndex)
        {
            auto *bone = skeleton->getBone(static_cast<int>(boneIndex));
            if (!bone)
                continue;

            FbxNode *boneNode = scene->FindNodeByName(bone->name.c_str());
            if (!boneNode || !hasAnyTransformCurve(boneNode, animationLayer))
                continue;

            auto keyTimes = collectTrackKeyTimes(boneNode, animationLayer, clipStart, clipEnd);
            if (keyTimes.empty())
                continue;

            AnimationTrack track;
            track.objectName = bone->name;
            track.keyFrames.reserve(keyTimes.size());

            for (const auto &keyTime : keyTimes)
            {
                auto localTransform = boneNode->EvaluateLocalTransform(keyTime);

                auto translation = localTransform.GetT();
                auto rotation = localTransform.GetQ();
                auto scale = localTransform.GetS();

                SQT keyFrame;
                keyFrame.position = glm::vec3(static_cast<float>(translation[0]), static_cast<float>(translation[1]), static_cast<float>(translation[2]));
                keyFrame.rotation = glm::normalize(glm::quat(static_cast<float>(rotation[3]), static_cast<float>(rotation[0]),
                                                             static_cast<float>(rotation[1]), static_cast<float>(rotation[2])));
                keyFrame.scale = glm::vec3(static_cast<float>(scale[0]), static_cast<float>(scale[1]), static_cast<float>(scale[2]));
                keyFrame.timeStamp = static_cast<float>(std::max(0.0, (keyTime - clipStart).GetSecondDouble() * animation.ticksPerSecond));

                track.keyFrames.push_back(keyFrame);
            }

            if (!track.keyFrames.empty())
                animation.boneAnimations.push_back(std::move(track));
        }

        if (!animation.boneAnimations.empty())
            animations.push_back(std::move(animation));
    }

    return animations;
}

void FBXAssetLoader::processNode(FbxNode *node, ProceedingMeshData &meshData)
{
    if (!node)
        return;

    FbxNodeAttribute *nodeAttribute = node->GetNodeAttribute();

    if (nodeAttribute)
        processNodeAttribute(nodeAttribute, node, meshData);

    for (int i = 0; i < node->GetChildCount(); ++i)
        processNode(node->GetChild(i), meshData);
}

void FBXAssetLoader::processNodeAttribute(FbxNodeAttribute *nodeAttribute, FbxNode *node, ProceedingMeshData &meshData)
{
    if (!nodeAttribute || !node)
        return;

    auto attributeType = nodeAttribute->GetAttributeType();

    if (attributeType == FbxNodeAttribute::eNull ||
        attributeType == FbxNodeAttribute::eUnknown)
        return;
    switch (attributeType)
    {
    case FbxNodeAttribute::eMesh:
    {
        processMesh(node, static_cast<FbxMesh *>(nodeAttribute), meshData);
        break;
    }
    case FbxNodeAttribute::eSkeleton:
    {
        processFbxSkeleton(node, static_cast<FbxSkeleton *>(nodeAttribute));
        break;
    }
    default:
    {
        VX_ENGINE_ERROR_STREAM("Unknow attribute type " << static_cast<int>(attributeType) << std::endl);
        break;
    }
    }
}

void FBXAssetLoader::processFbxSkeleton(FbxNode *node, FbxSkeleton *skeleton)
{
    switch (skeleton->GetSkeletonType())
    {
    case FbxSkeleton::eRoot: // one per skeleton
        return;
    case FbxSkeleton::eLimb: // bone
        return;
    case FbxSkeleton::eLimbNode: // end bone
        return;
    }
}

void FBXAssetLoader::processMesh(FbxNode *node, FbxMesh *mesh, ProceedingMeshData &meshData)
{
    if (!node || !mesh)
        return;

    if (int removedPolygons = mesh->RemoveBadPolygons(); removedPolygons == -1)
        VX_ENGINE_ERROR_STREAM("Failed to remove bad polygons\n");
    else if (removedPolygons > 0)
        VX_ENGINE_INFO_STREAM("Removed " << removedPolygons << " bad polygons\n");

    std::vector<TmpVertex> vertices;
    std::vector<uint32_t> indices;

    int vertexCount = mesh->GetControlPointsCount();
    fbxsdk::FbxVector4 *controlPoints = mesh->GetControlPoints();

    int polygonCount = mesh->GetPolygonCount();

    FbxStringList uvSetNameList;
    mesh->GetUVSetNames(uvSetNameList);

    const char *uvSetName = uvSetNameList.GetCount() > 0 ? uvSetNameList.GetStringAt(0) : nullptr;

    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;

    for (int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
    {
        int polygonSize = mesh->GetPolygonSize(polygonIndex);

        for (int vertexIndex = 0; vertexIndex < polygonSize; ++vertexIndex)
        {
            int controlPointIndex = mesh->GetPolygonVertex(polygonIndex, vertexIndex);

            auto position = controlPoints[controlPointIndex];

            FbxVector4 normal;

            mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal);

            FbxVector2 uv;
            bool unmapped;

            if (uvSetName)
                mesh->GetPolygonVertexUV(polygonIndex, vertexIndex, uvSetName, uv, unmapped);

            VertexKey key{controlPointIndex, normal, uv};

            if (vertexMap.find(key) == vertexMap.end())
            {
                TmpVertex v;
                v.position = glm::vec3((float)position[0], (float)position[1], (float)position[2]);
                v.normal = glm::vec3((float)normal[0], (float)normal[1], (float)normal[2]);
                v.textureCoordinates = glm::vec2((float)uv[0], (float)uv[1]);
                v.controlPointIndex = controlPointIndex;

                vertexMap[key] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(v);
            }

            indices.push_back(vertexMap[key]);
        }
    }

    FbxAMatrix localMatrix = node->EvaluateLocalTransform();

    glm::mat4 glmMatrix = glm::mat4(
        (float)localMatrix.Get(0, 0), (float)localMatrix.Get(0, 1), (float)localMatrix.Get(0, 2), (float)localMatrix.Get(0, 3),
        (float)localMatrix.Get(1, 0), (float)localMatrix.Get(1, 1), (float)localMatrix.Get(1, 2), (float)localMatrix.Get(1, 3),
        (float)localMatrix.Get(2, 0), (float)localMatrix.Get(2, 1), (float)localMatrix.Get(2, 2), (float)localMatrix.Get(2, 3),
        (float)localMatrix.Get(3, 0), (float)localMatrix.Get(3, 1), (float)localMatrix.Get(3, 2), (float)localMatrix.Get(3, 3));

    auto skeletonOptional = processSkeleton(mesh);

    CPUMesh cpuMesh;

    if (skeletonOptional.has_value())
    {
        std::vector<std::vector<Influence>> controlPointInfluences;
        controlPointInfluences.resize(mesh->GetControlPointsCount());

        auto *skin = static_cast<FbxSkin *>(mesh->GetDeformer(0, FbxDeformer::eSkin));
        int clusterCount = skin->GetClusterCount();
        auto &skeleton = skeletonOptional.value();
        meshData.skeleton = skeleton;

        for (int i = 0; i < clusterCount; ++i)
        {
            auto *cluster = skin->GetCluster(i);
            int boneId = skeleton.getBoneId(cluster->GetLink()->GetName());

            int indexCount = cluster->GetControlPointIndicesCount();
            int *indices = cluster->GetControlPointIndices();
            double *weights = cluster->GetControlPointWeights();

            for (int j = 0; j < indexCount; ++j)
            {
                int cp = indices[j];
                float w = static_cast<float>(weights[j]);

                if (w > 0.0f)
                    controlPointInfluences[cp].push_back({boneId, w});
            }
        }
        std::vector<vertex::VertexSkinned> skinnedVertices;

        for (const auto &vertex : vertices)
        {
            vertex::VertexSkinned v;
            v.position = vertex.position;
            v.normal = vertex.normal;
            v.textureCoordinates = vertex.textureCoordinates;
            v.boneIds = glm::ivec4(-1);
            v.weights = glm::vec4(0.0f);

            const auto &influences = controlPointInfluences[vertex.controlPointIndex];

            std::vector<Influence> sorted = influences;

            std::sort(sorted.begin(), sorted.end(),
                      [](const Influence &a, const Influence &b)
                      {
                          return a.weight > b.weight;
                      });

            float totalWeight = 0.0f;

            for (size_t i = 0; i < std::min<size_t>(4, sorted.size()); ++i)
            {
                v.boneIds[i] = sorted[i].boneId;
                v.weights[i] = sorted[i].weight;
                totalWeight += sorted[i].weight;
            }

            // Normalize
            if (totalWeight > 0.0f)
                v.weights /= totalWeight;

            skinnedVertices.push_back(v);
        }

        cpuMesh = CPUMesh::build<vertex::VertexSkinned>(skinnedVertices, indices);
    }
    else
    {
        std::vector<vertex::Vertex3D> vertices3D;

        for (const auto &vertex : vertices)
        {
            vertex::Vertex3D v;
            v.position = vertex.position;
            v.normal = vertex.normal;
            v.textureCoordinates = vertex.textureCoordinates;
            vertices3D.push_back(v);
        }

        cpuMesh = CPUMesh::build<vertex::Vertex3D>(vertices3D, indices);
    }

    cpuMesh.localTransform = glmMatrix;

    std::string meshName = mesh->GetName() ? mesh->GetName() : "";
    if (meshName.empty())
        meshName = node->GetName() ? node->GetName() : "";
    if (meshName.empty())
        meshName = "Mesh_" + std::to_string(meshData.meshes.size());
    cpuMesh.name = meshName;

    processMaterials(node, cpuMesh);

    meshData.meshes.push_back(cpuMesh);
}

std::optional<Skeleton> FBXAssetLoader::processSkeleton(FbxMesh *mesh)
{
    if (mesh->GetDeformerCount(FbxDeformer::eSkin) == 0)
        return std::nullopt;

    Skeleton skeleton;

    auto skin = static_cast<FbxSkin *>(
        mesh->GetDeformer(0, FbxDeformer::eSkin));

    const int clusterCount = skin->GetClusterCount();

    VX_ENGINE_INFO_STREAM("Has " << clusterCount << " bones\n");

    for (int i = 0; i < clusterCount; ++i)
    {
        FbxCluster *cluster = skin->GetCluster(i);
        FbxNode *boneNode = cluster->GetLink();

        if (!boneNode)
            continue;

        Skeleton::BoneInfo bone;
        bone.id = i;
        bone.name = boneNode->GetName();
        FbxAMatrix local = boneNode->EvaluateLocalTransform();

        bone.localBindTransform = glm::mat4(
            (float)local.Get(0, 0), (float)local.Get(0, 1), (float)local.Get(0, 2), (float)local.Get(0, 3),
            (float)local.Get(1, 0), (float)local.Get(1, 1), (float)local.Get(1, 2), (float)local.Get(1, 3),
            (float)local.Get(2, 0), (float)local.Get(2, 1), (float)local.Get(2, 2), (float)local.Get(2, 3),
            (float)local.Get(3, 0), (float)local.Get(3, 1), (float)local.Get(3, 2), (float)local.Get(3, 3));

        FbxAMatrix meshTransform;
        FbxAMatrix boneTransform;

        cluster->GetTransformMatrix(meshTransform);
        cluster->GetTransformLinkMatrix(boneTransform);

        FbxAMatrix offset = boneTransform.Inverse() * meshTransform;

        bone.offsetMatrix = glm::mat4(
            (float)offset.Get(0, 0), (float)offset.Get(0, 1), (float)offset.Get(0, 2), (float)offset.Get(0, 3),
            (float)offset.Get(1, 0), (float)offset.Get(1, 1), (float)offset.Get(1, 2), (float)offset.Get(1, 3),
            (float)offset.Get(2, 0), (float)offset.Get(2, 1), (float)offset.Get(2, 2), (float)offset.Get(2, 3),
            (float)offset.Get(3, 0), (float)offset.Get(3, 1), (float)offset.Get(3, 2), (float)offset.Get(3, 3));

        skeleton.addBone(bone);

        // VX_ENGINE_INFO_STREAM("Added " << bone.name << " bone \n");
    }

    return skeleton;
}

void FBXAssetLoader::processMaterials(FbxNode *node, CPUMesh &mesh)
{
    if (!node)
        return;

    int materialCount = node->GetMaterialCount();

    if (materialCount == 0)
        return;

    for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        FbxSurfaceMaterial *material = node->GetMaterial(materialIndex);
        if (!material)
            continue;

        const char *matName = material->GetName();
        mesh.material.name = std::string(matName);
        VX_ENGINE_INFO_STREAM(mesh.material.name << std::endl);
    }
}

ELIX_NESTED_NAMESPACE_END
