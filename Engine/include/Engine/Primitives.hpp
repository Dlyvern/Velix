#ifndef PRIMITIVES_HPP
#define PRIMITIVES_HPP

#include <vector>
#include "Engine/Vertex.hpp"
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace triangle
{
    const std::vector<vertex::Vertex2D> vertices =
        {
            {{0.0f, -0.5f, 0.0f}, {0.5f, 0.0f}},
            {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}},
    };

    const std::vector<uint32_t> indices =
        {
            0, 1, 2};
} // namespace triangle

namespace quad
{
    static std::vector<vertex::Vertex2D> vertices =
        {
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
            {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};

    static std::vector<uint32_t> indices =
        {
            0, 1, 2,
            2, 3, 0};

} // namespace quad

namespace cube
{
    static std::vector<vertex::Vertex3D> vertices =
        {
            // front +Z
            {{-0.5f, -0.5f, 0.5f}, {0, 0}, {0, 0, 1}},
            {{0.5f, -0.5f, 0.5f}, {1, 0}, {0, 0, 1}},
            {{0.5f, 0.5f, 0.5f}, {1, 1}, {0, 0, 1}},
            {{-0.5f, 0.5f, 0.5f}, {0, 1}, {0, 0, 1}},

            // back -Z
            {{0.5f, -0.5f, -0.5f}, {0, 0}, {0, 0, -1}},
            {{-0.5f, -0.5f, -0.5f}, {1, 0}, {0, 0, -1}},
            {{-0.5f, 0.5f, -0.5f}, {1, 1}, {0, 0, -1}},
            {{0.5f, 0.5f, -0.5f}, {0, 1}, {0, 0, -1}},

            // left -X
            {{-0.5f, -0.5f, -0.5f}, {0, 0}, {-1, 0, 0}},
            {{-0.5f, -0.5f, 0.5f}, {1, 0}, {-1, 0, 0}},
            {{-0.5f, 0.5f, 0.5f}, {1, 1}, {-1, 0, 0}},
            {{-0.5f, 0.5f, -0.5f}, {0, 1}, {-1, 0, 0}},

            // right +X
            {{0.5f, -0.5f, 0.5f}, {0, 0}, {1, 0, 0}},
            {{0.5f, -0.5f, -0.5f}, {1, 0}, {1, 0, 0}},
            {{0.5f, 0.5f, -0.5f}, {1, 1}, {1, 0, 0}},
            {{0.5f, 0.5f, 0.5f}, {0, 1}, {1, 0, 0}},

            // top +Y
            {{-0.5f, 0.5f, 0.5f}, {0, 0}, {0, 1, 0}},
            {{0.5f, 0.5f, 0.5f}, {1, 0}, {0, 1, 0}},
            {{0.5f, 0.5f, -0.5f}, {1, 1}, {0, 1, 0}},
            {{-0.5f, 0.5f, -0.5f}, {0, 1}, {0, 1, 0}},

            // bottom -Y
            {{-0.5f, -0.5f, -0.5f}, {0, 0}, {0, -1, 0}},
            {{0.5f, -0.5f, -0.5f}, {1, 0}, {0, -1, 0}},
            {{0.5f, -0.5f, 0.5f}, {1, 1}, {0, -1, 0}},
            {{-0.5f, -0.5f, 0.5f}, {0, 1}, {0, -1, 0}},
    };

    static std::vector<uint32_t> indices =
        {
            0, 1, 2, 2, 3, 0,
            4, 5, 6, 6, 7, 4,
            8, 9, 10, 10, 11, 8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20};

} // namespace cube

ELIX_NESTED_NAMESPACE_END

#endif // PRIMITIVES_HPP