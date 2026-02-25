#ifndef ELIX_PRIMITIVES_HPP
#define ELIX_PRIMITIVES_HPP

#ifdef _WIN32
#define _USE_MATH_DEFINES
#endif

#include <vector>
#include "Engine/Vertex.hpp"
#include <cstdint>
#include <cmath>
#include "Engine/Mesh.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace triangle
{
    static const std::vector<vertex::Vertex2D> vertices =
        {
            {{0.0f, -0.5f, 0.0f}, {0.5f, 0.0f}},
            {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}},
    };

    static const std::vector<uint32_t> indices =
        {
            0, 1, 2};
} // namespace triangle

namespace quad
{
    static const std::vector<vertex::Vertex2D> vertices =
        {
            {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, 0.0f}, {1.0f, 0.0f}},
            {{0.5f, 0.5f, 0.0f}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, 0.0f}, {0.0f, 1.0f}}};

    static const std::vector<uint32_t> indices =
        {
            0, 1, 2,
            2, 3, 0};

} // namespace quad

namespace cube
{
    static const std::vector<vertex::Vertex3D> vertices =
        {
            {{-0.5f, -0.5f, 0.5f}, {0, 0}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
            {{0.5f, -0.5f, 0.5f}, {1, 0}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
            {{0.5f, 0.5f, 0.5f}, {1, 1}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
            {{-0.5f, 0.5f, 0.5f}, {0, 1}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}},

            {{0.5f, -0.5f, -0.5f}, {0, 0}, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
            {{-0.5f, -0.5f, -0.5f}, {1, 0}, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
            {{-0.5f, 0.5f, -0.5f}, {1, 1}, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
            {{0.5f, 0.5f, -0.5f}, {0, 1}, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},

            {{-0.5f, -0.5f, -0.5f}, {0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
            {{-0.5f, -0.5f, 0.5f}, {1, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
            {{-0.5f, 0.5f, 0.5f}, {1, 1}, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
            {{-0.5f, 0.5f, -0.5f}, {0, 1}, {-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},

            {{0.5f, -0.5f, 0.5f}, {0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
            {{0.5f, -0.5f, -0.5f}, {1, 0}, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
            {{0.5f, 0.5f, -0.5f}, {1, 1}, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
            {{0.5f, 0.5f, 0.5f}, {0, 1}, {1, 0, 0}, {0, 0, -1}, {0, 1, 0}},

            {{-0.5f, 0.5f, 0.5f}, {0, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
            {{0.5f, 0.5f, 0.5f}, {1, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
            {{0.5f, 0.5f, -0.5f}, {1, 1}, {0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
            {{-0.5f, 0.5f, -0.5f}, {0, 1}, {0, 1, 0}, {1, 0, 0}, {0, 0, -1}},

            {{-0.5f, -0.5f, -0.5f}, {0, 0}, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
            {{0.5f, -0.5f, -0.5f}, {1, 0}, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
            {{0.5f, -0.5f, 0.5f}, {1, 1}, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
            {{-0.5f, -0.5f, 0.5f}, {0, 1}, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
    };

    static const std::vector<uint32_t> indices =
        {
            0, 1, 2, 2, 3, 0,
            4, 5, 6, 6, 7, 4,
            8, 9, 10, 10, 11, 8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20};

} // namespace cube

namespace circle
{
    inline void genereteVerticesAndIndices(std::vector<vertex::Vertex3D> &vertices, std::vector<uint32_t> &indices,
                                           float radius = 1.0f)
    {
        int sectorCount = 32;
        int stackCount = 16;
        float sectorStep = 2 * M_PI / sectorCount;
        float stackStep = M_PI / stackCount;

        for (int i = 0; i <= stackCount; ++i)
        {
            float stackAngle = M_PI / 2 - i * stackStep;
            float xy = radius * cosf(stackAngle);
            float z = radius * sinf(stackAngle);

            for (int j = 0; j <= sectorCount; ++j)
            {
                float sectorAngle = j * sectorStep;

                vertex::Vertex3D vertex;
                vertex.position.x = xy * cosf(sectorAngle);
                vertex.position.y = xy * sinf(sectorAngle);
                vertex.position.z = z;

                vertex.normal = glm::normalize(vertex.position);

                vertex.textureCoordinates.x = (float)j / sectorCount;
                vertex.textureCoordinates.y = (float)i / stackCount;

                vertices.push_back(vertex);
            }
        }
        for (int i = 0; i < stackCount; ++i)
        {
            int k1 = i * (sectorCount + 1);
            int k2 = k1 + sectorCount + 1;

            for (int j = 0; j < sectorCount; ++j, ++k1, ++k2)
            {
                if (i != 0)
                {
                    indices.push_back(k1);
                    indices.push_back(k2);
                    indices.push_back(k1 + 1);
                }

                if (i != (stackCount - 1))
                {
                    indices.push_back(k1 + 1);
                    indices.push_back(k2);
                    indices.push_back(k2 + 1);
                }
            }
        }

        vertex::generateTangents(vertices, indices);
    }

}

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_PRIMITIVES_HPP