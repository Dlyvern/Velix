#ifndef ELIX_RENDER_GRAPH_PASS_CONTEXT_HPP
#define ELIX_RENDER_GRAPH_PASS_CONTEXT_HPP

#include "Core/Macros.hpp"

#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class RenderGraphPassContext
{
public:
    uint32_t currentFrame;
    uint32_t currentImageIndex;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_GRAPH_PASS_CONTEXT_HPP