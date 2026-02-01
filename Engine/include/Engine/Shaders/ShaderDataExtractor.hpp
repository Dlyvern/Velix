#ifndef ELIX_SHADER_DATA_EXTRACTOR_HPP
#define ELIX_SHADER_DATA_EXTRACTOR_HPP

#include "Core/ShaderHandler.hpp"

#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct ShaderReflection
{
};

class ShaderDataExtractor
{
public:
    static std::vector<ShaderReflection> parse(const core::ShaderHandler &shaderHandler);
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SHADER_DATA_EXTRACTOR_HPP