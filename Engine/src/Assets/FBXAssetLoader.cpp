#include "Engine/Assets/FBXAssetLoader.hpp"

#include <iostream>

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

    if (!importer->Initialize(filePath.c_str(), -1, m_fbxManager->GetIOSettings()))
    {
        std::cerr << "Failed to initialize FBX importer: " << importer->GetStatus().GetErrorString() << std::endl;
        importer->Destroy();
        return {};
    }
    
    FbxScene* scene = FbxScene::Create(m_fbxManager, "scene");
    importer->Import(scene);
    importer->Destroy();
    
    FbxNode* rootNode = scene->GetRootNode();

    // if (rootNode)
    //     ProcessNode(rootNode);
    // else
    //     std::cerr << "Root node is invalid" << std::endl;
        
    scene->Destroy();

    return {};
}

ELIX_NESTED_NAMESPACE_END