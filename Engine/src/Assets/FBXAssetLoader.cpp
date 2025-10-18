#include "Engine/Assets/FBXAssetLoader.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

FBXAssetLoader::FBXAssetLoader()
{

}

const std::vector<std::string> FBXAssetLoader::getSupportedFormats() const
{
    return {".fbx", ".FBX"};
}

void FBXAssetLoader::load(const std::string& filePath)
{

}

bool FBXAssetLoader::canLoad(const std::string& extension)
{   
    const auto supportedFormats = getSupportedFormats();

    std::find_if(supportedFormats.begin(), supportedFormats.end(), [&extension](const auto& format) {return format == extension; });


}

ELIX_NESTED_NAMESPACE_END