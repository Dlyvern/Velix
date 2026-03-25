#ifndef ELIX_DEBUG_DRAW_HPP
#define ELIX_DEBUG_DRAW_HPP

#include "Core/Macros.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <vector>
#include <mutex>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class DebugDraw
{
public:
    struct Vertex
    {
        float x, y, z;    // world-space position
        float r, g, b, a; // linear colour
    };

    static void line(glm::vec3 a, glm::vec3 b,
                     glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
                     float lifetime = 0.0f,
                     bool depthTest = true);

    static void box(glm::mat4 transform,
                    glm::vec3 halfExtents,
                    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
                    float lifetime = 0.0f,
                    bool depthTest = true);

    static void sphere(glm::vec3 center,
                       float radius,
                       glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
                       float lifetime = 0.0f,
                       bool depthTest = true,
                       int segments = 16);

    static void frustum(glm::mat4 invViewProj,
                        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
                        float lifetime = 0.0f);

    static void raycast(glm::vec3 origin,
                        glm::vec3 direction,
                        float length,
                        glm::vec4 color = {1.0f, 0.2f, 0.2f, 1.0f},
                        float lifetime = 0.0f);

    static void capsule(glm::vec3 base,
                        glm::vec3 tip,
                        float radius,
                        glm::vec4 color = {0.2f, 1.0f, 0.2f, 1.0f},
                        float lifetime = 0.0f);
    static void cone(glm::vec3 apex,
                     glm::vec3 direction,
                     float length,
                     float halfAngleDeg,
                     glm::vec4 color = {1.0f, 1.0f, 0.2f, 1.0f},
                     float lifetime = 0.0f,
                     int segments = 16);

    static void aabb(glm::vec3 min,
                     glm::vec3 max,
                     glm::vec4 color = {0.2f, 0.8f, 1.0f, 1.0f},
                     float lifetime = 0.0f);

    static void cross(glm::vec3 center,
                      float size,
                      float lifetime = 0.0f);

    static void collectVertices(std::vector<Vertex> &out);

    static void flush(float deltaTime);

private:
    struct Shape
    {
        std::vector<Vertex> vertices;
        float lifetime; // remaining seconds; 0 = this-frame-only
        bool depthTest; // stored for potential future multi-pipeline support
    };

    static std::vector<Shape> &shapes();
    static std::mutex &mutex();

    static void pushShape(std::vector<Vertex> verts, float lifetime, bool depthTest);

    // Helpers
    static Vertex makeVertex(glm::vec3 p, glm::vec4 c);
    static void addLine(std::vector<Vertex> &v, glm::vec3 a, glm::vec3 b, glm::vec4 c);
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DEBUG_DRAW_HPP
