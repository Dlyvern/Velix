#ifndef ELIX_IASSET_LOADER_HPP
#define ELIX_IASSET_LOADER_HPP

#include "Core/Macros.hpp"

#include <vector>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IAssetLoader
{
public:
    virtual const std::vector<std::string> getSupportedFormats() const = 0;
    virtual void load(const std::string& filePath) = 0;
    virtual bool canLoad(const std::string& extension) = 0;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_IASSET_LOADER_HPP