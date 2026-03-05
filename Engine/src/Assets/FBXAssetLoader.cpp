#include "Engine/Assets/FBXAssetLoader.hpp"

#include <iostream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>

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

struct Influences
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
    bool looksLikeWindowsAbsolutePath(const std::string &path)
    {
        return path.size() >= 3u &&
               std::isalpha(static_cast<unsigned char>(path[0])) &&
               path[1] == ':' &&
               (path[2] == '\\' || path[2] == '/');
    }

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

    std::string normalizeFbxTexturePath(const std::string &rawPath, const std::filesystem::path &assetDirectory)
    {
        if (rawPath.empty())
            return {};

        std::string portablePath = rawPath;
        std::replace(portablePath.begin(), portablePath.end(), '\\', '/');
        std::filesystem::path texturePath(portablePath);

        std::error_code errorCode;
        if (texturePath.is_absolute() && std::filesystem::exists(texturePath, errorCode) && !errorCode)
            return texturePath.lexically_normal().string();

        // FBX can contain authoring-machine absolute paths (Windows drive path).
        // Try local alternatives before keeping original unresolved path.
        if (looksLikeWindowsAbsolutePath(portablePath))
        {
            const std::filesystem::path fileName = texturePath.filename();
            if (!fileName.empty())
            {
                std::filesystem::path candidate = (assetDirectory / fileName).lexically_normal();
                errorCode.clear();
                if (std::filesystem::exists(candidate, errorCode) && !errorCode)
                    return candidate.string();

                const std::filesystem::path parentDirectoryName = texturePath.parent_path().filename();
                if (!parentDirectoryName.empty())
                {
                    candidate = (assetDirectory / parentDirectoryName / fileName).lexically_normal();
                    errorCode.clear();
                    if (std::filesystem::exists(candidate, errorCode) && !errorCode)
                        return candidate.string();
                }
            }
        }

        const std::filesystem::path candidate = (assetDirectory / texturePath).lexically_normal();
        if (std::filesystem::exists(candidate, errorCode) && !errorCode)
            return candidate.string();

        return texturePath.lexically_normal().string();
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace
{
    glm::mat4 toGlmMatrix(const FbxAMatrix &matrix)
    {
        return glm::mat4(
            static_cast<float>(matrix.Get(0, 0)), static_cast<float>(matrix.Get(0, 1)), static_cast<float>(matrix.Get(0, 2)), static_cast<float>(matrix.Get(0, 3)),
            static_cast<float>(matrix.Get(1, 0)), static_cast<float>(matrix.Get(1, 1)), static_cast<float>(matrix.Get(1, 2)), static_cast<float>(matrix.Get(1, 3)),
            static_cast<float>(matrix.Get(2, 0)), static_cast<float>(matrix.Get(2, 1)), static_cast<float>(matrix.Get(2, 2)), static_cast<float>(matrix.Get(2, 3)),
            static_cast<float>(matrix.Get(3, 0)), static_cast<float>(matrix.Get(3, 1)), static_cast<float>(matrix.Get(3, 2)), static_cast<float>(matrix.Get(3, 3)));
    }

    FbxAMatrix getNodeGeometricTransform(FbxNode *node)
    {
        if (!node)
            return FbxAMatrix();

        const FbxVector4 geometricTranslation = node->GetGeometricTranslation(FbxNode::eSourcePivot);
        const FbxVector4 geometricRotation = node->GetGeometricRotation(FbxNode::eSourcePivot);
        const FbxVector4 geometricScaling = node->GetGeometricScaling(FbxNode::eSourcePivot);
        return FbxAMatrix(geometricTranslation, geometricRotation, geometricScaling);
    }

    FbxAMatrix getSceneRootInverseGlobalTransform(FbxNode *node)
    {
        if (!node)
            return FbxAMatrix();

        auto *scene = node->GetScene();
        if (!scene)
            return FbxAMatrix();

        auto *rootNode = scene->GetRootNode();
        if (!rootNode)
            return FbxAMatrix();

        return rootNode->EvaluateGlobalTransform().Inverse();
    }

    FbxAMatrix getNodeGlobalTransformRelativeToSceneRoot(FbxNode *node)
    {
        if (!node)
            return FbxAMatrix();

        return getSceneRootInverseGlobalTransform(node) * node->EvaluateGlobalTransform();
    }

    bool isIdentityMatrix(const glm::mat4 &matrix)
    {
        constexpr float epsilon = 0.0001f;
        const glm::mat4 identity(1.0f);

        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (std::fabs(matrix[column][row] - identity[column][row]) > epsilon)
                    return false;

        return true;
    }

    void resetSkeletonHierarchy(Skeleton &skeleton)
    {
        for (size_t boneIndex = 0; boneIndex < skeleton.getBonesCount(); ++boneIndex)
        {
            auto *bone = skeleton.getBone(static_cast<int>(boneIndex));
            if (!bone)
                continue;

            bone->parentId = -1;
            bone->children.clear();
            bone->childrenInfo.clear();
        }
    }

    void addBoneChainToSkeleton(FbxNode *boneNode, Skeleton &skeleton)
    {
        if (!boneNode)
            return;

        std::vector<FbxNode *> chain;
        for (auto *current = boneNode; current; current = current->GetParent())
            chain.push_back(current);

        for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
        {
            FbxNode *node = *iterator;
            if (!node || !node->GetName() || node->GetName()[0] == '\0')
                continue;

            const std::string boneName = node->GetName();
            if (skeleton.hasBone(boneName))
                continue;

            Skeleton::BoneInfo bone;
            bone.name = boneName;
            bone.localBindTransform = toGlmMatrix(node->EvaluateLocalTransform());
            bone.globalBindTransform = toGlmMatrix(getNodeGlobalTransformRelativeToSceneRoot(node));
            bone.offsetMatrix = glm::mat4(1.0f);
            skeleton.addBone(bone);
        }
    }

    void mergeSkeletonData(Skeleton &target, Skeleton &source)
    {
        for (size_t boneIndex = 0; boneIndex < source.getBonesCount(); ++boneIndex)
        {
            auto *sourceBone = source.getBone(static_cast<int>(boneIndex));
            if (!sourceBone)
                continue;

            auto *targetBone = target.getBone(sourceBone->name);
            if (!targetBone)
            {
                Skeleton::BoneInfo newBone = *sourceBone;
                newBone.id = -1;
                newBone.parentId = -1;
                newBone.children.clear();
                newBone.childrenInfo.clear();
                target.addBone(newBone);
                targetBone = target.getBone(sourceBone->name);
            }

            if (!targetBone)
                continue;

            if (isIdentityMatrix(targetBone->localBindTransform) && !isIdentityMatrix(sourceBone->localBindTransform))
                targetBone->localBindTransform = sourceBone->localBindTransform;

            if (isIdentityMatrix(targetBone->globalBindTransform) && !isIdentityMatrix(sourceBone->globalBindTransform))
                targetBone->globalBindTransform = sourceBone->globalBindTransform;

            // Preserve the first valid inverse bind matrix to keep a stable global palette.
            if (isIdentityMatrix(targetBone->offsetMatrix) && !isIdentityMatrix(sourceBone->offsetMatrix))
                targetBone->offsetMatrix = sourceBone->offsetMatrix;
        }
    }

    int findNearestAncestorBoneId(FbxNode *node, Skeleton &skeleton)
    {
        if (!node)
            return -1;

        // Prefer the nearest transform carrier (mesh node itself first), then walk parents.
        // This preserves rigid attachments exported as animated helper/null nodes.
        for (FbxNode *current = node; current; current = current->GetParent())
        {
            const char *nodeName = current->GetName();
            if (!nodeName || nodeName[0] == '\0')
                continue;

            int boneId = skeleton.getBoneId(nodeName);
            if (boneId >= 0)
                return boneId;

            addBoneChainToSkeleton(current, skeleton);

            boneId = skeleton.getBoneId(nodeName);
            if (boneId >= 0)
                return boneId;
        }

        return -1;
    }
} // namespace

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
}

FBXAssetLoader::~FBXAssetLoader()
{
    if (m_fbxManager)
    {
        m_fbxManager->Destroy();
        m_fbxManager = nullptr;
        m_fbxIOSettings = nullptr;
    }
}

const std::vector<std::string> FBXAssetLoader::getSupportedFormats() const
{
    return {".fbx", ".FBX"};
}

std::shared_ptr<IAsset> FBXAssetLoader::load(const std::string &filePath)
{
    return loadInternal(filePath);
}

std::shared_ptr<IAsset> FBXAssetLoader::loadInternal(const std::string &filePath)
{
    resetImportCaches();

    if (m_fbxIOSettings)
    {
        // Keep material/texture link metadata for path extraction, but skip optional payloads.
        m_fbxIOSettings->SetBoolProp(IMP_FBX_MATERIAL, true);
        m_fbxIOSettings->SetBoolProp(IMP_FBX_TEXTURE, true);
        m_fbxIOSettings->SetBoolProp(IMP_FBX_LINK, true);
        m_fbxIOSettings->SetBoolProp(IMP_FBX_MODEL, true);
        m_fbxIOSettings->SetBoolProp(IMP_FBX_SHAPE, false);
        m_fbxIOSettings->SetBoolProp(IMP_FBX_GOBO, false);
        m_fbxIOSettings->SetBoolProp(IMP_FBX_ANIMATION, true);
        m_fbxIOSettings->SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);
        m_fbxIOSettings->SetBoolProp(IMP_FBX_EXTRACT_EMBEDDED_DATA, false);
        // Some assets trigger Autodesk awTess assertions during importer-side triangulation/cleanup.
        // Keep those paths off and rely on our own mesh sanitization + fan triangulation.
        m_fbxIOSettings->SetBoolProp(IMP_GEOMETRY "|" IOSN_TRIANGULATE, false);
        m_fbxIOSettings->SetBoolProp(IMP_REMOVEBADPOLYSFROMMESH, false);
    }

    FbxImporter *importer = FbxImporter::Create(m_fbxManager, "");

    ProceedingMeshData meshesData;

    m_currentAssetDirectory = std::filesystem::path(filePath).parent_path().lexically_normal();

    if (!importer->Initialize(filePath.c_str(), -1, m_fbxManager->GetIOSettings()))
    {
        VX_ENGINE_ERROR_STREAM("Failed to initialize FBX importer: " << importer->GetStatus().GetErrorString() << std::endl);
        importer->Destroy();
        resetImportCaches();
        return nullptr;
    }

    FbxScene *scene = FbxScene::Create(m_fbxManager, "scene");

    if (!importer->Import(scene))
    {
        VX_ENGINE_ERROR_STREAM("Failed to import FBX: " << importer->GetStatus().GetErrorString() << std::endl);
        importer->Destroy();
        scene->Destroy();
        resetImportCaches();
        return nullptr;
    }

    importer->Destroy();

    // Avoid FBX SDK global triangulation here.
    // Some Blender-exported ngons can trigger asserts in Autodesk's awTess
    // path (inside Triangulate). We triangulate safely in processMesh() by
    // fan-splitting polygons and filtering degenerate triangles.

    FbxNode *rootNode = scene->GetRootNode();

    if (!rootNode)
    {
        VX_ENGINE_ERROR_STREAM("Root node is invalid\n");
        scene->Destroy();
        resetImportCaches();
        return nullptr;
    }

    processNode(rootNode, meshesData);

    if (meshesData.skeleton.has_value())
    {
        auto &skeleton = meshesData.skeleton.value();
        resetSkeletonHierarchy(skeleton);
        buildSkeletonHierarchy(scene->GetRootNode(), skeleton);
    }

    const std::vector<Animation> animations = processAnimations(scene, meshesData.skeleton);

    VX_ENGINE_INFO_STREAM("Found " << animations.size() << " animations\n");

    scene->Destroy();

    VX_ENGINE_INFO_STREAM("Parsed " << meshesData.meshes.size() << " meshes\n");

    auto modelAsset = std::make_shared<ModelAsset>(meshesData.meshes,
                                                   meshesData.skeleton,
                                                   animations);

    resetImportCaches();
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
        processFbxSkeleton(node, static_cast<FbxSkeleton *>(nodeAttribute), meshData);
        break;
    }
    default:
    {
        // Ignore unsupported node attribute types (camera/light/etc.) while importing meshes.
        break;
    }
    }
}

void FBXAssetLoader::processFbxSkeleton(FbxNode *node, FbxSkeleton *skeleton, ProceedingMeshData &meshData)
{
    if (!node || !skeleton)
        return;

    if (!meshData.skeleton.has_value())
        meshData.skeleton = Skeleton{};

    const char *nodeName = node->GetName();
    if (!nodeName || nodeName[0] == '\0')
        return;

    if (!meshData.skeleton->hasBone(nodeName))
    {
        Skeleton::BoneInfo bone;
        bone.name = nodeName;
        bone.localBindTransform = toGlmMatrix(node->EvaluateLocalTransform());
        bone.globalBindTransform = toGlmMatrix(getNodeGlobalTransformRelativeToSceneRoot(node));
        bone.offsetMatrix = glm::mat4(1.0f);
        meshData.skeleton->addBone(bone);
    }

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

    // NOTE: avoid mesh->RemoveBadPolygons() because on some malformed ngons it enters
    // the same awTess path that can assert-abort. We sanitize and skip bad triangles
    // below during import.

    struct SubmeshBuildData
    {
        std::vector<TmpVertex> vertices;
        std::vector<uint32_t> indices;
        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;
    };

    std::unordered_map<int, SubmeshBuildData> submeshesByMaterial;
    std::vector<int> materialOrder;

    fbxsdk::FbxVector4 *controlPoints = mesh->GetControlPoints();
    const int polygonCount = mesh->GetPolygonCount();

    if (!controlPoints || polygonCount <= 0)
        return;

    FbxStringList uvSetNameList;
    mesh->GetUVSetNames(uvSetNameList);

    const FbxGeometryElementUV *primaryUvElement = mesh->GetElementUV(0);
    std::unordered_map<std::string, const FbxGeometryElementUV *> uvElementsByName;
    std::vector<const FbxGeometryElementUV *> uvElements;
    uvElements.reserve(static_cast<size_t>(std::max(0, mesh->GetElementUVCount())));
    for (int uvElementIndex = 0; uvElementIndex < mesh->GetElementUVCount(); ++uvElementIndex)
    {
        const auto *uvElement = mesh->GetElementUV(uvElementIndex);
        if (!uvElement)
            continue;

        uvElements.push_back(uvElement);
        const char *uvElementName = uvElement->GetName();
        if (uvElementName && uvElementName[0] != '\0')
            uvElementsByName.emplace(uvElementName, uvElement);
    }
    const char *uvSetName = nullptr;
    if (primaryUvElement && primaryUvElement->GetName() && primaryUvElement->GetName()[0] != '\0')
        uvSetName = primaryUvElement->GetName();
    else if (uvSetNameList.GetCount() > 0)
        uvSetName = uvSetNameList.GetStringAt(0);

    std::vector<std::string> uvSetNames;
    uvSetNames.reserve(static_cast<size_t>(std::max(0, uvSetNameList.GetCount())));
    for (int uvSetIndex = 0; uvSetIndex < uvSetNameList.GetCount(); ++uvSetIndex)
    {
        const char *candidateUvSetName = uvSetNameList.GetStringAt(uvSetIndex);
        if (!candidateUvSetName || candidateUvSetName[0] == '\0')
            continue;
        uvSetNames.emplace_back(candidateUvSetName);
    }

    if (uvSetName && uvSetName[0] != '\0')
    {
        const auto hasPrimaryName = std::find(uvSetNames.begin(), uvSetNames.end(), std::string(uvSetName)) != uvSetNames.end();
        if (!hasPrimaryName)
            uvSetNames.insert(uvSetNames.begin(), uvSetName);
    }

    const FbxGeometryElementMaterial *materialElement = mesh->GetElementMaterial();
    const int nodeMaterialCount = node->GetMaterialCount();
    std::unordered_map<int, std::vector<std::string>> uvSetNamesByMaterialSlot;

    if (node && nodeMaterialCount > 0)
    {
        uvSetNamesByMaterialSlot.reserve(static_cast<size_t>(nodeMaterialCount));
        for (int materialIndex = 0; materialIndex < nodeMaterialCount; ++materialIndex)
        {
            std::vector<std::string> candidateUvSetNames = uvSetNames;

            if (auto *slotMaterial = node->GetMaterial(materialIndex))
            {
                auto cacheIterator = m_materialImportCache.find(slotMaterial);
                if (cacheIterator == m_materialImportCache.end())
                    cacheIterator = m_materialImportCache.emplace(slotMaterial, parseMaterial(slotMaterial)).first;

                const std::string &preferredUvSetName = cacheIterator->second.uvSetName;
                if (!preferredUvSetName.empty())
                {
                    candidateUvSetNames.erase(
                        std::remove(candidateUvSetNames.begin(), candidateUvSetNames.end(), preferredUvSetName),
                        candidateUvSetNames.end());
                    candidateUvSetNames.insert(candidateUvSetNames.begin(), preferredUvSetName);
                }
            }

            uvSetNamesByMaterialSlot.emplace(materialIndex, std::move(candidateUvSetNames));
        }
    }

    auto isFiniteVec3 = [](const glm::vec3 &value) -> bool
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };

    auto isFiniteVec2 = [](const glm::vec2 &value) -> bool
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    };

    auto isDegenerateTriangle = [&](const std::vector<TmpVertex> &submeshVertices, uint32_t index0, uint32_t index1, uint32_t index2) -> bool
    {
        if (index0 == index1 || index0 == index2 || index1 == index2)
            return true;

        if (index0 >= submeshVertices.size() || index1 >= submeshVertices.size() || index2 >= submeshVertices.size())
            return true;

        const glm::vec3 &p0 = submeshVertices[index0].position;
        const glm::vec3 &p1 = submeshVertices[index1].position;
        const glm::vec3 &p2 = submeshVertices[index2].position;

        if (!isFiniteVec3(p0) || !isFiniteVec3(p1) || !isFiniteVec3(p2))
            return true;

        const glm::vec3 edge01 = p1 - p0;
        const glm::vec3 edge02 = p2 - p0;
        const float areaTwice = glm::length(glm::cross(edge01, edge02));
        return !std::isfinite(areaTwice) || areaTwice <= 1e-10f;
    };

    auto triangulatePolygonEarClipping = [&](const std::vector<TmpVertex> &submeshVertices,
                                             const std::vector<uint32_t> &polygon,
                                             std::vector<uint32_t> &outTriangles) -> bool
    {
        if (polygon.size() < 3)
            return false;

        if (polygon.size() == 3)
        {
            const uint32_t index0 = polygon[0];
            const uint32_t index1 = polygon[1];
            const uint32_t index2 = polygon[2];
            if (isDegenerateTriangle(submeshVertices, index0, index1, index2))
                return false;

            outTriangles.push_back(index0);
            outTriangles.push_back(index1);
            outTriangles.push_back(index2);
            return true;
        }

        glm::vec3 newellNormal(0.0f);
        for (size_t index = 0; index < polygon.size(); ++index)
        {
            const glm::vec3 &current = submeshVertices[polygon[index]].position;
            const glm::vec3 &next = submeshVertices[polygon[(index + 1u) % polygon.size()]].position;
            newellNormal.x += (current.y - next.y) * (current.z + next.z);
            newellNormal.y += (current.z - next.z) * (current.x + next.x);
            newellNormal.z += (current.x - next.x) * (current.y + next.y);
        }

        const glm::vec3 absNormal = glm::abs(newellNormal);
        int dropAxis = 2;
        if (absNormal.x > absNormal.y && absNormal.x > absNormal.z)
            dropAxis = 0;
        else if (absNormal.y > absNormal.z)
            dropAxis = 1;

        std::vector<glm::vec2> projectedPoints;
        projectedPoints.reserve(polygon.size());
        for (const uint32_t vertexIndex : polygon)
        {
            const glm::vec3 &position = submeshVertices[vertexIndex].position;
            if (dropAxis == 0)
                projectedPoints.emplace_back(position.y, position.z);
            else if (dropAxis == 1)
                projectedPoints.emplace_back(position.x, position.z);
            else
                projectedPoints.emplace_back(position.x, position.y);
        }

        auto polygonSignedArea2D = [](const std::vector<glm::vec2> &points) -> float
        {
            if (points.size() < 3)
                return 0.0f;

            double signedArea = 0.0;
            for (size_t index = 0; index < points.size(); ++index)
            {
                const glm::vec2 &current = points[index];
                const glm::vec2 &next = points[(index + 1u) % points.size()];
                signedArea += static_cast<double>(current.x) * static_cast<double>(next.y) -
                              static_cast<double>(next.x) * static_cast<double>(current.y);
            }

            return static_cast<float>(signedArea * 0.5);
        };

        auto signedArea = polygonSignedArea2D(projectedPoints);
        if (!std::isfinite(signedArea) || std::abs(signedArea) <= 1e-10f)
            return false;

        const float windingSign = signedArea >= 0.0f ? 1.0f : -1.0f;

        auto pointInTriangle = [](const glm::vec2 &point,
                                  const glm::vec2 &a,
                                  const glm::vec2 &b,
                                  const glm::vec2 &c) -> bool
        {
            const float area1 = (point.x - b.x) * (a.y - b.y) - (a.x - b.x) * (point.y - b.y);
            const float area2 = (point.x - c.x) * (b.y - c.y) - (b.x - c.x) * (point.y - c.y);
            const float area3 = (point.x - a.x) * (c.y - a.y) - (c.x - a.x) * (point.y - a.y);

            const bool hasNegative = (area1 < -1e-8f) || (area2 < -1e-8f) || (area3 < -1e-8f);
            const bool hasPositive = (area1 > 1e-8f) || (area2 > 1e-8f) || (area3 > 1e-8f);
            return !(hasNegative && hasPositive);
        };

        std::vector<int> remaining;
        remaining.reserve(static_cast<int>(polygon.size()));
        for (int index = 0; index < static_cast<int>(polygon.size()); ++index)
            remaining.push_back(index);

        int guardCounter = 0;
        const int maxGuard = static_cast<int>(polygon.size() * polygon.size());
        while (remaining.size() > 3 && guardCounter < maxGuard)
        {
            ++guardCounter;
            bool clippedEar = false;

            for (size_t earIndex = 0; earIndex < remaining.size(); ++earIndex)
            {
                const size_t prevEarIndex = (earIndex + remaining.size() - 1u) % remaining.size();
                const size_t nextEarIndex = (earIndex + 1u) % remaining.size();

                const int prevIndex = remaining[prevEarIndex];
                const int currentIndex = remaining[earIndex];
                const int nextIndex = remaining[nextEarIndex];

                const glm::vec2 &a = projectedPoints[prevIndex];
                const glm::vec2 &b = projectedPoints[currentIndex];
                const glm::vec2 &c = projectedPoints[nextIndex];

                const float cross2D = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (cross2D * windingSign <= 1e-8f)
                    continue;

                bool hasInnerPoint = false;
                for (const int testIndex : remaining)
                {
                    if (testIndex == prevIndex || testIndex == currentIndex || testIndex == nextIndex)
                        continue;

                    if (pointInTriangle(projectedPoints[testIndex], a, b, c))
                    {
                        hasInnerPoint = true;
                        break;
                    }
                }

                if (hasInnerPoint)
                    continue;

                const uint32_t index0 = polygon[static_cast<size_t>(prevIndex)];
                const uint32_t index1 = polygon[static_cast<size_t>(currentIndex)];
                const uint32_t index2 = polygon[static_cast<size_t>(nextIndex)];
                if (isDegenerateTriangle(submeshVertices, index0, index1, index2))
                    continue;

                outTriangles.push_back(index0);
                outTriangles.push_back(index1);
                outTriangles.push_back(index2);
                remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(earIndex));
                clippedEar = true;
                break;
            }

            if (!clippedEar)
                break;
        }

        if (remaining.size() != 3)
            return false;

        const uint32_t index0 = polygon[static_cast<size_t>(remaining[0])];
        const uint32_t index1 = polygon[static_cast<size_t>(remaining[1])];
        const uint32_t index2 = polygon[static_cast<size_t>(remaining[2])];
        if (isDegenerateTriangle(submeshVertices, index0, index1, index2))
            return false;

        outTriangles.push_back(index0);
        outTriangles.push_back(index1);
        outTriangles.push_back(index2);
        return true;
    };

    auto resolvePolygonMaterialIndex = [&](int polygonIndex) -> int
    {
        int materialIndex = 0;
        if (materialElement)
        {
            switch (materialElement->GetMappingMode())
            {
            case FbxGeometryElement::eByPolygon:
                if (polygonIndex >= 0 && polygonIndex < materialElement->GetIndexArray().GetCount())
                    materialIndex = materialElement->GetIndexArray().GetAt(polygonIndex);
                break;
            case FbxGeometryElement::eAllSame:
                if (materialElement->GetIndexArray().GetCount() > 0)
                    materialIndex = materialElement->GetIndexArray().GetAt(0);
                break;
            default:
                break;
            }
        }

        if (materialIndex < 0)
            materialIndex = 0;

        if (nodeMaterialCount > 0 && materialIndex >= nodeMaterialCount)
            materialIndex = nodeMaterialCount - 1;

        return materialIndex;
    };

    auto sampleFromUvElement = [&](const FbxGeometryElementUV *uvElement,
                                   int polygonIndex,
                                   int vertexIndex,
                                   int controlPointIndex,
                                   FbxVector2 &outUv) -> bool
    {
        if (!uvElement)
            return false;

        int uvElementIndex = -1;
        switch (uvElement->GetMappingMode())
        {
        case FbxGeometryElement::eByControlPoint:
            uvElementIndex = controlPointIndex;
            break;
        case FbxGeometryElement::eByPolygonVertex:
        {
            const int polygonVertexBaseIndex = mesh->GetPolygonVertexIndex(polygonIndex);
            if (polygonVertexBaseIndex < 0)
                return false;
            uvElementIndex = polygonVertexBaseIndex + vertexIndex;
            break;
        }
        case FbxGeometryElement::eByPolygon:
            uvElementIndex = polygonIndex;
            break;
        case FbxGeometryElement::eAllSame:
            uvElementIndex = 0;
            break;
        default:
            return false;
        }

        if (uvElementIndex < 0)
            return false;

        const auto &directArray = uvElement->GetDirectArray();
        const auto &indexArray = uvElement->GetIndexArray();

        switch (uvElement->GetReferenceMode())
        {
        case FbxGeometryElement::eDirect:
            if (uvElementIndex >= directArray.GetCount())
                return false;
            outUv = directArray.GetAt(uvElementIndex);
            return true;
        case FbxGeometryElement::eIndexToDirect:
            if (uvElementIndex >= indexArray.GetCount())
                return false;
            {
                const int directIndex = indexArray.GetAt(uvElementIndex);
                if (directIndex < 0 || directIndex >= directArray.GetCount())
                    return false;
                outUv = directArray.GetAt(directIndex);
                return true;
            }
        default:
            return false;
        }
    };

    auto samplePolygonVertexUV = [&](int polygonIndex, int vertexIndex, int controlPointIndex, int materialIndex) -> FbxVector2
    {
        FbxVector2 uv(0.0, 0.0);
        const std::vector<std::string> *candidateUvSetNames = &uvSetNames;

        const auto slotUvSetIterator = uvSetNamesByMaterialSlot.find(materialIndex);
        if (slotUvSetIterator != uvSetNamesByMaterialSlot.end() && !slotUvSetIterator->second.empty())
            candidateUvSetNames = &slotUvSetIterator->second;

        if (candidateUvSetNames && !candidateUvSetNames->empty())
        {
            for (const auto &candidateUvSetName : *candidateUvSetNames)
            {
                bool unmapped = false;
                const bool hasUV = mesh->GetPolygonVertexUV(polygonIndex, vertexIndex, candidateUvSetName.c_str(), uv, unmapped);
                if (hasUV && !unmapped)
                    return uv;

                if (const auto uvElementIterator = uvElementsByName.find(candidateUvSetName); uvElementIterator != uvElementsByName.end())
                {
                    if (sampleFromUvElement(uvElementIterator->second, polygonIndex, vertexIndex, controlPointIndex, uv))
                        return uv;
                }
            }
        }
        else if (uvSetName)
        {
            bool unmapped = false;
            const bool hasUV = mesh->GetPolygonVertexUV(polygonIndex, vertexIndex, uvSetName, uv, unmapped);
            if (hasUV && !unmapped)
                return uv;
        }

        if (sampleFromUvElement(primaryUvElement, polygonIndex, vertexIndex, controlPointIndex, uv))
            return uv;

        for (const auto *uvElement : uvElements)
            if (sampleFromUvElement(uvElement, polygonIndex, vertexIndex, controlPointIndex, uv))
                return uv;

        return uv;
    };

    for (int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
    {
        const int polygonSize = mesh->GetPolygonSize(polygonIndex);
        if (polygonSize < 3)
            continue;

        const int materialIndex = resolvePolygonMaterialIndex(polygonIndex);
        auto submeshIterator = submeshesByMaterial.find(materialIndex);
        if (submeshIterator == submeshesByMaterial.end())
        {
            materialOrder.push_back(materialIndex);
            submeshIterator = submeshesByMaterial.emplace(materialIndex, SubmeshBuildData{}).first;
        }
        SubmeshBuildData &submesh = submeshIterator->second;

        std::vector<uint32_t> polygonVertexIndices;
        polygonVertexIndices.reserve(static_cast<size_t>(polygonSize));

        for (int vertexIndex = 0; vertexIndex < polygonSize; ++vertexIndex)
        {
            const int controlPointIndex = mesh->GetPolygonVertex(polygonIndex, vertexIndex);
            if (controlPointIndex < 0 || controlPointIndex >= mesh->GetControlPointsCount())
                continue;

            auto position = controlPoints[controlPointIndex];
            const glm::vec3 positionVec(
                static_cast<float>(position[0]),
                static_cast<float>(position[1]),
                static_cast<float>(position[2]));
            if (!isFiniteVec3(positionVec))
                continue;

            FbxVector4 normal;
            normal.Set(0.0, 1.0, 0.0, 0.0);

            mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal);
            const glm::vec3 normalVec(
                static_cast<float>(normal[0]),
                static_cast<float>(normal[1]),
                static_cast<float>(normal[2]));
            const glm::vec3 safeNormal =
                isFiniteVec3(normalVec) && glm::length(normalVec) > 1e-8f
                    ? glm::normalize(normalVec)
                    : glm::vec3(0.0f, 1.0f, 0.0f);

            const FbxVector2 uv = samplePolygonVertexUV(polygonIndex, vertexIndex, controlPointIndex, materialIndex);
            const glm::vec2 uvVec(static_cast<float>(uv[0]), static_cast<float>(uv[1]));
            const glm::vec2 safeUv = isFiniteVec2(uvVec) ? uvVec : glm::vec2(0.0f);

            VertexKey key{
                controlPointIndex,
                FbxVector4(safeNormal.x, safeNormal.y, safeNormal.z, 0.0),
                FbxVector2(safeUv.x, safeUv.y)};

            auto mapIterator = submesh.vertexMap.find(key);
            if (mapIterator == submesh.vertexMap.end())
            {
                TmpVertex vertexData;
                vertexData.position = positionVec;
                vertexData.normal = safeNormal;
                vertexData.textureCoordinates = safeUv;
                vertexData.controlPointIndex = controlPointIndex;

                const uint32_t newVertexIndex = static_cast<uint32_t>(submesh.vertices.size());
                submesh.vertices.push_back(vertexData);
                mapIterator = submesh.vertexMap.emplace(key, newVertexIndex).first;
            }

            polygonVertexIndices.push_back(mapIterator->second);
        }

        std::vector<uint32_t> polygonTriangles;
        polygonTriangles.reserve((polygonVertexIndices.size() - 2u) * 3u);
        if (!triangulatePolygonEarClipping(submesh.vertices, polygonVertexIndices, polygonTriangles))
        {
            for (size_t i = 1; i + 1 < polygonVertexIndices.size(); ++i)
            {
                const uint32_t index0 = polygonVertexIndices[0];
                const uint32_t index1 = polygonVertexIndices[i];
                const uint32_t index2 = polygonVertexIndices[i + 1];

                if (isDegenerateTriangle(submesh.vertices, index0, index1, index2))
                    continue;

                polygonTriangles.push_back(index0);
                polygonTriangles.push_back(index1);
                polygonTriangles.push_back(index2);
            }
        }

        submesh.indices.insert(submesh.indices.end(), polygonTriangles.begin(), polygonTriangles.end());
    }

    if (materialOrder.empty())
    {
        materialOrder.push_back(0);
        submeshesByMaterial.emplace(0, SubmeshBuildData{});
    }

    // We flatten FBX hierarchy into per-mesh transforms on one engine entity.
    // Use transform relative to FBX scene root so parent node transforms are preserved.
    FbxAMatrix meshNodeTransform = node->EvaluateGlobalTransform();
    if (auto *scene = node->GetScene())
    {
        if (auto *rootNode = scene->GetRootNode())
            meshNodeTransform = rootNode->EvaluateGlobalTransform().Inverse() * meshNodeTransform;
    }
    meshNodeTransform = meshNodeTransform * getNodeGeometricTransform(node);

    glm::mat4 glmMatrix = toGlmMatrix(meshNodeTransform);

    std::optional<Skeleton> skeletonOptional = processSkeleton(node, mesh);
    Skeleton *globalSkeleton = nullptr;

    if (skeletonOptional.has_value())
    {
        if (!meshData.skeleton.has_value())
            meshData.skeleton = Skeleton{};

        mergeSkeletonData(meshData.skeleton.value(), skeletonOptional.value());
        globalSkeleton = &meshData.skeleton.value();
    }

    const bool isSkinnedMesh = (globalSkeleton != nullptr);

    std::vector<std::vector<Influences>> controlPointInfluences;
    if (globalSkeleton)
    {
        controlPointInfluences.resize(mesh->GetControlPointsCount());

        const int skinDeformerCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
        for (int skinIndex = 0; skinIndex < skinDeformerCount; ++skinIndex)
        {
            auto *skin = static_cast<FbxSkin *>(mesh->GetDeformer(skinIndex, FbxDeformer::eSkin));
            if (!skin)
                continue;

            const int clusterCount = skin->GetClusterCount();
            for (int i = 0; i < clusterCount; ++i)
            {
                auto *cluster = skin->GetCluster(i);
                if (!cluster || !cluster->GetLink())
                    continue;

                const int boneId = globalSkeleton->getBoneId(cluster->GetLink()->GetName());
                if (boneId < 0 || static_cast<size_t>(boneId) >= globalSkeleton->getBonesCount())
                    continue;

                const int indexCount = cluster->GetControlPointIndicesCount();
                int *controlPointIndices = cluster->GetControlPointIndices();
                double *weights = cluster->GetControlPointWeights();

                if (!controlPointIndices || !weights)
                    continue;

                for (int j = 0; j < indexCount; ++j)
                {
                    const int cp = controlPointIndices[j];
                    const float w = static_cast<float>(weights[j]);

                    if (cp < 0 || cp >= static_cast<int>(controlPointInfluences.size()))
                        continue;

                    if (std::isfinite(w) && w > 0.0f)
                        controlPointInfluences[cp].push_back({boneId, w});
                }
            }
        }
    }

    std::string meshNameBase = mesh->GetName() ? mesh->GetName() : "";
    if (meshNameBase.empty())
        meshNameBase = node->GetName() ? node->GetName() : "";
    if (meshNameBase.empty())
        meshNameBase = "Mesh_" + std::to_string(meshData.meshes.size());

    const bool hasMaterialSplits = materialOrder.size() > 1u;
    size_t emittedSubmeshCount = 0u;

    for (size_t submeshOrderIndex = 0; submeshOrderIndex < materialOrder.size(); ++submeshOrderIndex)
    {
        const int materialIndex = materialOrder[submeshOrderIndex];
        auto submeshIterator = submeshesByMaterial.find(materialIndex);
        if (submeshIterator == submeshesByMaterial.end())
            continue;

        auto &submesh = submeshIterator->second;
        if (submesh.vertices.empty() || submesh.indices.empty())
            continue;

        CPUMesh cpuMesh;
        if (isSkinnedMesh)
        {
            std::vector<vertex::VertexSkinned> skinnedVertices;
            skinnedVertices.reserve(submesh.vertices.size());

            for (const auto &vertex : submesh.vertices)
            {
                vertex::VertexSkinned v;
                v.position = vertex.position;
                v.normal = vertex.normal;
                v.textureCoordinates = vertex.textureCoordinates;
                v.boneIds = glm::ivec4(-1);
                v.weights = glm::vec4(0.0f);

                const auto &influences = controlPointInfluences[vertex.controlPointIndex];

                std::vector<Influences> sorted;
                sorted.reserve(influences.size());
                for (const auto &influence : influences)
                {
                    if (influence.boneId < 0 || static_cast<size_t>(influence.boneId) >= globalSkeleton->getBonesCount())
                        continue;
                    if (!std::isfinite(influence.weight) || influence.weight <= 0.0f)
                        continue;
                    sorted.push_back(influence);
                }

                std::sort(sorted.begin(), sorted.end(),
                          [](const Influences &a, const Influences &b)
                          {
                              return a.weight > b.weight;
                          });

                float totalWeight = 0.0f;
                for (size_t influenceIndex = 0; influenceIndex < std::min<size_t>(4, sorted.size()); ++influenceIndex)
                {
                    v.boneIds[influenceIndex] = sorted[influenceIndex].boneId;
                    v.weights[influenceIndex] = sorted[influenceIndex].weight;
                    totalWeight += sorted[influenceIndex].weight;
                }

                if (totalWeight > 0.0f)
                    v.weights /= totalWeight;

                skinnedVertices.push_back(v);
            }

            std::vector<vertex::Vertex3D> tangentVertices;
            tangentVertices.reserve(skinnedVertices.size());
            for (const auto &vertex : skinnedVertices)
                tangentVertices.emplace_back(vertex.position, vertex.textureCoordinates, vertex.normal, glm::vec3(0.0f), glm::vec3(0.0f));

            vertex::generateTangents(tangentVertices, submesh.indices);

            for (size_t vertexIndex = 0; vertexIndex < skinnedVertices.size() && vertexIndex < tangentVertices.size(); ++vertexIndex)
            {
                skinnedVertices[vertexIndex].tangent = tangentVertices[vertexIndex].tangent;
                skinnedVertices[vertexIndex].bitangent = tangentVertices[vertexIndex].bitangent;
            }

            cpuMesh = CPUMesh::build<vertex::VertexSkinned>(skinnedVertices, submesh.indices);
        }
        else
        {
            std::vector<vertex::Vertex3D> vertices3D;
            vertices3D.reserve(submesh.vertices.size());

            for (const auto &vertex : submesh.vertices)
            {
                vertex::Vertex3D v;
                v.position = vertex.position;
                v.normal = vertex.normal;
                v.textureCoordinates = vertex.textureCoordinates;
                vertices3D.push_back(v);
            }

            cpuMesh = CPUMesh::build<vertex::Vertex3D>(vertices3D, submesh.indices);
        }

        if (!isSkinnedMesh && meshData.skeleton.has_value())
            cpuMesh.attachedBoneId = findNearestAncestorBoneId(node, meshData.skeleton.value());

        if (isSkinnedMesh)
            cpuMesh.localTransform = glm::mat4(1.0f);
        else if (cpuMesh.attachedBoneId >= 0 && meshData.skeleton.has_value())
        {
            if (auto *attachmentBone = meshData.skeleton->getBone(cpuMesh.attachedBoneId))
                cpuMesh.localTransform = glm::inverse(attachmentBone->globalBindTransform) * glmMatrix;
            else
                cpuMesh.localTransform = glmMatrix;
        }
        else
            cpuMesh.localTransform = glmMatrix;

        cpuMesh.name = meshNameBase;
        if (hasMaterialSplits)
        {
            std::string materialSuffix;
            if (materialIndex >= 0 && materialIndex < nodeMaterialCount)
            {
                if (auto *slotMaterial = node->GetMaterial(materialIndex))
                {
                    const char *materialName = slotMaterial->GetName();
                    if (materialName && materialName[0] != '\0')
                        materialSuffix = materialName;
                }
            }

            if (materialSuffix.empty())
                materialSuffix = "mat_" + std::to_string(materialIndex);

            cpuMesh.name += "_" + materialSuffix;
        }

        processMaterials(node, cpuMesh, materialIndex);

        if (cpuMesh.vertexData.empty() || cpuMesh.indices.empty())
        {
            VX_ENGINE_WARNING_STREAM("Skipping empty FBX mesh \"" << cpuMesh.name << "\" from node \""
                                                                  << (node->GetName() ? node->GetName() : "<unnamed>")
                                                                  << "\"\n");
            continue;
        }

        meshData.meshes.push_back(cpuMesh);
        ++emittedSubmeshCount;
    }

    if (emittedSubmeshCount == 0u)
    {
        VX_ENGINE_WARNING_STREAM("Skipping empty FBX mesh \"" << meshNameBase << "\" from node \""
                                                              << (node->GetName() ? node->GetName() : "<unnamed>")
                                                              << "\"\n");
    }
}

std::optional<Skeleton> FBXAssetLoader::processSkeleton(FbxNode *node, FbxMesh *mesh)
{
    if (!mesh || mesh->GetDeformerCount(FbxDeformer::eSkin) == 0)
        return std::nullopt;

    Skeleton skeleton;
    int totalClusterCount = 0;
    const FbxAMatrix meshGeometryTransform = getNodeGeometricTransform(node);
    const FbxAMatrix sceneRootInverseGlobal = getSceneRootInverseGlobalTransform(node);

    const int skinDeformerCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
    for (int skinIndex = 0; skinIndex < skinDeformerCount; ++skinIndex)
    {
        auto *skin = static_cast<FbxSkin *>(mesh->GetDeformer(skinIndex, FbxDeformer::eSkin));
        if (!skin)
            continue;

        const int clusterCount = skin->GetClusterCount();
        totalClusterCount += clusterCount;

        for (int i = 0; i < clusterCount; ++i)
        {
            FbxCluster *cluster = skin->GetCluster(i);
            if (!cluster)
                continue;

            FbxNode *boneNode = cluster->GetLink();
            if (!boneNode || !boneNode->GetName() || boneNode->GetName()[0] == '\0')
                continue;

            addBoneChainToSkeleton(boneNode, skeleton);
            auto *bone = skeleton.getBone(boneNode->GetName());

            if (!bone)
                continue;

            bone->localBindTransform = toGlmMatrix(boneNode->EvaluateLocalTransform());

            FbxAMatrix meshTransform;
            FbxAMatrix boneTransform;

            cluster->GetTransformMatrix(meshTransform);
            cluster->GetTransformLinkMatrix(boneTransform);

            const FbxAMatrix meshTransformRelative = sceneRootInverseGlobal * meshTransform;
            const FbxAMatrix boneTransformRelative = sceneRootInverseGlobal * boneTransform;

            bone->globalBindTransform = toGlmMatrix(boneTransformRelative);

            FbxAMatrix offset = boneTransformRelative.Inverse() * meshTransformRelative * meshGeometryTransform;
            bone->offsetMatrix = toGlmMatrix(offset);
        }
    }

    VX_ENGINE_INFO_STREAM("Has " << totalClusterCount << " skin clusters and " << skeleton.getBonesCount() << " merged bones\n");

    if (skeleton.getBonesCount() == 0)
        return std::nullopt;

    return skeleton;
}

void FBXAssetLoader::processMaterials(FbxNode *node, CPUMesh &mesh, int materialSlot)
{
    if (!node)
        return;

    const int materialCount = node->GetMaterialCount();

    if (materialCount == 0)
        return;

    auto applyMaterial = [this, &mesh](FbxSurfaceMaterial *material)
    {
        if (!material)
            return;

        auto cacheIterator = m_materialImportCache.find(material);
        if (cacheIterator == m_materialImportCache.end())
            cacheIterator = m_materialImportCache.emplace(material, parseMaterial(material)).first;

        const ImportedMaterialData &importedMaterial = cacheIterator->second;
        mesh.material.name = importedMaterial.name;
        mesh.material.albedoTexture = importedMaterial.albedoTexture;
        mesh.material.normalTexture = importedMaterial.normalTexture;
        mesh.material.emissiveTexture = importedMaterial.emissiveTexture;
    };

    if (materialSlot >= 0 && materialSlot < materialCount)
    {
        applyMaterial(node->GetMaterial(materialSlot));
        return;
    }

    for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        applyMaterial(node->GetMaterial(materialIndex));
        if (!mesh.material.albedoTexture.empty() ||
            !mesh.material.normalTexture.empty() ||
            !mesh.material.emissiveTexture.empty())
            break;
    }
}

FBXAssetLoader::ImportedMaterialData FBXAssetLoader::parseMaterial(FbxSurfaceMaterial *material)
{
    ImportedMaterialData data{};
    if (!material)
        return data;

    const char *materialName = material->GetName();
    if (materialName)
        data.name = materialName;

    auto extractTextureAndUvSet = [this, &data](FbxProperty property) -> std::string
    {
        std::string propertyUvSetName;
        const std::string texturePath = extractTexturePathFromProperty(property, &propertyUvSetName);
        if (data.uvSetName.empty() && !propertyUvSetName.empty())
            data.uvSetName = propertyUvSetName;
        return texturePath;
    };

    data.albedoTexture = extractTextureAndUvSet(material->FindProperty(FbxSurfaceMaterial::sDiffuse));
    data.normalTexture = extractTextureAndUvSet(material->FindProperty(FbxSurfaceMaterial::sNormalMap));
    if (data.normalTexture.empty())
        data.normalTexture = extractTextureAndUvSet(material->FindProperty(FbxSurfaceMaterial::sBump));
    data.emissiveTexture = extractTextureAndUvSet(material->FindProperty(FbxSurfaceMaterial::sEmissive));

    return data;
}

std::string FBXAssetLoader::extractTexturePathFromProperty(FbxProperty property, std::string *outUvSetName)
{
    if (!property.IsValid())
        return {};

    if (outUvSetName)
        outUvSetName->clear();

    const int fileTextureCount = property.GetSrcObjectCount<FbxFileTexture>();
    for (int textureIndex = 0; textureIndex < fileTextureCount; ++textureIndex)
    {
        auto *fileTexture = property.GetSrcObject<FbxFileTexture>(textureIndex);
        if (!fileTexture)
            continue;

        if (outUvSetName && outUvSetName->empty())
        {
            const FbxString uvSetName = fileTexture->UVSet.Get();
            const char *uvSetNameData = uvSetName.Buffer();
            if (uvSetNameData && uvSetNameData[0] != '\0')
                *outUvSetName = uvSetNameData;
        }

        std::string texturePath = fileTexture->GetFileName() ? fileTexture->GetFileName() : "";
        if (texturePath.empty() && fileTexture->GetRelativeFileName())
            texturePath = fileTexture->GetRelativeFileName();

        if (!texturePath.empty())
            return normalizeTexturePath(texturePath);
    }

    const int layeredTextureCount = property.GetSrcObjectCount<FbxLayeredTexture>();
    for (int layeredIndex = 0; layeredIndex < layeredTextureCount; ++layeredIndex)
    {
        auto *layeredTexture = property.GetSrcObject<FbxLayeredTexture>(layeredIndex);
        if (!layeredTexture)
            continue;

        const int layeredFileTextureCount = layeredTexture->GetSrcObjectCount<FbxFileTexture>();
        for (int textureIndex = 0; textureIndex < layeredFileTextureCount; ++textureIndex)
        {
            auto *fileTexture = layeredTexture->GetSrcObject<FbxFileTexture>(textureIndex);
            if (!fileTexture)
                continue;

            if (outUvSetName && outUvSetName->empty())
            {
                const FbxString uvSetName = fileTexture->UVSet.Get();
                const char *uvSetNameData = uvSetName.Buffer();
                if (uvSetNameData && uvSetNameData[0] != '\0')
                    *outUvSetName = uvSetNameData;
            }

            std::string texturePath = fileTexture->GetFileName() ? fileTexture->GetFileName() : "";
            if (texturePath.empty() && fileTexture->GetRelativeFileName())
                texturePath = fileTexture->GetRelativeFileName();

            if (!texturePath.empty())
                return normalizeTexturePath(texturePath);
        }
    }

    return {};
}

std::string FBXAssetLoader::normalizeTexturePath(const std::string &rawPath)
{
    if (rawPath.empty())
        return {};

    std::string cacheKey = rawPath;
    std::replace(cacheKey.begin(), cacheKey.end(), '\\', '/');

    const auto cacheIterator = m_texturePathResolveCache.find(cacheKey);
    if (cacheIterator != m_texturePathResolveCache.end())
        return cacheIterator->second;

    std::string normalizedPath = normalizeFbxTexturePath(cacheKey, m_currentAssetDirectory);
    m_texturePathResolveCache.emplace(std::move(cacheKey), normalizedPath);
    return normalizedPath;
}

void FBXAssetLoader::resetImportCaches()
{
    m_materialImportCache.clear();
    m_texturePathResolveCache.clear();
}

ELIX_NESTED_NAMESPACE_END
