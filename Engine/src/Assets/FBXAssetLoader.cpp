#include "Engine/Assets/FBXAssetLoader.hpp"

#include <iostream>

struct VertexKey 
{
    int controlPoint;
    FbxVector4 normal;
    FbxVector2 uv;

    bool operator==(const VertexKey& other) const 
    {
        return controlPoint == other.controlPoint &&
               normal == other.normal &&
               uv == other.uv;
    }
};

struct VertexKeyHash 
{
    size_t operator()(const VertexKey& key) const 
    {
        auto h1 = std::hash<int>()(key.controlPoint);
        auto h2 = std::hash<double>()(key.normal[0] + key.normal[1] + key.normal[2]);
        auto h3 = std::hash<double>()(key.uv[0] + key.uv[1]);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

inline glm::mat4 toGLM(const float* m)
{
    // Assumes column-major order like OpenGL
    return glm::mat4(
        m[0],  m[1],  m[2],  m[3],
        m[4],  m[5],  m[6],  m[7],
        m[8],  m[9],  m[10], m[11],
        m[12], m[13], m[14], m[15]
    );
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

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

std::shared_ptr<IAsset> FBXAssetLoader::load(const std::string& filePath)
{
    FbxImporter* importer = FbxImporter::Create(m_fbxManager, "");

    std::vector<Mesh3D> meshes;

    if (!importer->Initialize(filePath.c_str(), -1, m_fbxManager->GetIOSettings()))
    {
        std::cerr << "Failed to initialize FBX importer: " << importer->GetStatus().GetErrorString() << std::endl;
        importer->Destroy();
        return {};
    }
    
    FbxScene* scene = FbxScene::Create(m_fbxManager, "scene");
    importer->Import(scene);
    importer->Destroy();

    FbxGeometryConverter converter(m_fbxManager);
    converter.Triangulate(scene, true);
    
    FbxNode* rootNode = scene->GetRootNode();

    if (rootNode)
        processNode(rootNode, meshes);
    else
        std::cerr << "Root node is invalid\n";
        
    scene->Destroy();

    std::cout << "Parsed " << meshes.size() << " meshes\n";

    auto modelAsset = std::make_shared<ModelAsset>(meshes);

    return modelAsset;
}

void FBXAssetLoader::processNode(FbxNode* node, std::vector<Mesh3D>& meshes)
{
    if(!node)
        return;
    
    FbxNodeAttribute* nodeAttribute = node->GetNodeAttribute();

    if(nodeAttribute)
        processNodeAttribute(nodeAttribute, node, meshes);

    for(int i = 0;i < node->GetChildCount(); ++i)
        processNode(node->GetChild(i), meshes);
}

void FBXAssetLoader::processNodeAttribute(FbxNodeAttribute* nodeAttribute, FbxNode* node, std::vector<Mesh3D>& meshes)
{
    if(!nodeAttribute || !node)
        return;

    auto attributeType = nodeAttribute->GetAttributeType();

    switch(attributeType)
    {
        case FbxNodeAttribute::eMesh:
        {
            processMesh(node, static_cast<FbxMesh*>(nodeAttribute), meshes);
            break;
        }
        default:
        {
            std::cerr << "Unnknow attribute type " << static_cast<int>(attributeType) << std::endl;
            break;
        }
    }
}

void FBXAssetLoader::processMesh(FbxNode* node, FbxMesh* mesh, std::vector<Mesh3D>& meshes)
{
    if(!node || !mesh)
        return;

    if(int removedPolygons = mesh->RemoveBadPolygons(); removedPolygons == -1)
        std::cerr << "Failed to remove bad polygons\n";
    else if(removedPolygons > 0)
        std::cout << "Removed " << removedPolygons << " bad polygons\n";

    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    int vertexCount = mesh->GetControlPointsCount();
    fbxsdk::FbxVector4* controlPoints = mesh->GetControlPoints();

    int polygonCount = mesh->GetPolygonCount();

    FbxStringList uvSetNameList;
    mesh->GetUVSetNames(uvSetNameList);

    const char* uvSetName = uvSetNameList.GetCount() > 0 ? uvSetNameList.GetStringAt(0) : nullptr;

    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;

    for(int polygonIndex = 0; polygonIndex < polygonCount; ++polygonIndex)
    {
        int polygonSize = mesh->GetPolygonSize(polygonIndex);

        for(int vertexIndex = 0; vertexIndex < polygonSize; ++vertexIndex)
        {
            int controlPointIndex = mesh->GetPolygonVertex(polygonIndex, vertexIndex);

            auto position = controlPoints[controlPointIndex];
            
            FbxVector4 normal;

            mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, normal);

            FbxVector2 uv;
            bool unmapped;

            if(uvSetName)
                mesh->GetPolygonVertexUV(polygonIndex, vertexIndex, uvSetName, uv, unmapped);

            VertexKey key{controlPointIndex, normal, uv};

            if (vertexMap.find(key) == vertexMap.end())
            {
                Vertex3D v;
                v.position = glm::vec3((float)position[0], (float)position[1], (float)position[2]);
                v.normal = glm::vec3((float)normal[0], (float)normal[1], (float)normal[2]);
                v.textureCoordinates = glm::vec2((float)uv[0], (float)uv[1]);

                vertexMap[key] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(v);
            }

            indices.push_back(vertexMap[key]);
        }
    }

    auto& cpuMesh = meshes.emplace_back(Mesh3D{vertices, indices});

    FbxAMatrix localMatrix = node->EvaluateLocalTransform();

    glm::mat4 glmMatrix = glm::mat4(
        (float)localMatrix.Get(0,0), (float)localMatrix.Get(0,1), (float)localMatrix.Get(0,2), (float)localMatrix.Get(0,3),
        (float)localMatrix.Get(1,0), (float)localMatrix.Get(1,1), (float)localMatrix.Get(1,2), (float)localMatrix.Get(1,3),
        (float)localMatrix.Get(2,0), (float)localMatrix.Get(2,1), (float)localMatrix.Get(2,2), (float)localMatrix.Get(2,3),
        (float)localMatrix.Get(3,0), (float)localMatrix.Get(3,1), (float)localMatrix.Get(3,2), (float)localMatrix.Get(3,3)
    );

    cpuMesh.localTransform = glmMatrix;
    
    processMaterials(node, cpuMesh);
}

void FBXAssetLoader::processMaterials(FbxNode* node, Mesh3D& mesh)
{
    if (!node) return;

    int materialCount = node->GetMaterialCount();

    if (materialCount == 0)
        return;

    for (int materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        FbxSurfaceMaterial* material = node->GetMaterial(materialIndex);
        if (!material) continue;

        const char* matName = material->GetName();
        mesh.material.name = std::string(matName);
        std::cout << mesh.material.name << std::endl;
    }
}

ELIX_NESTED_NAMESPACE_END