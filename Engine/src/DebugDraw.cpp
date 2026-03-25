#include "Engine/DebugDraw.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/transform.hpp>
#include <cmath>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

std::vector<DebugDraw::Shape> &DebugDraw::shapes()
{
    static std::vector<Shape> s;
    return s;
}

std::mutex &DebugDraw::mutex()
{
    static std::mutex m;
    return m;
}

DebugDraw::Vertex DebugDraw::makeVertex(glm::vec3 p, glm::vec4 c)
{
    return {p.x, p.y, p.z, c.r, c.g, c.b, c.a};
}

void DebugDraw::addLine(std::vector<Vertex> &v, glm::vec3 a, glm::vec3 b, glm::vec4 c)
{
    v.push_back(makeVertex(a, c));
    v.push_back(makeVertex(b, c));
}

void DebugDraw::pushShape(std::vector<Vertex> verts, float lifetime, bool depthTest)
{
    std::lock_guard<std::mutex> lock(mutex());
    shapes().push_back({std::move(verts), lifetime, depthTest});
}

void DebugDraw::line(glm::vec3 a, glm::vec3 b, glm::vec4 color, float lifetime, bool depthTest)
{
    std::vector<Vertex> v;
    v.reserve(2);
    addLine(v, a, b, color);
    pushShape(std::move(v), lifetime, depthTest);
}

void DebugDraw::box(glm::mat4 transform, glm::vec3 e, glm::vec4 color, float lifetime, bool depthTest)
{
    // 8 corners in local space (±e)
    glm::vec4 corners[8] = {
        {-e.x, -e.y, -e.z, 1},
        {e.x, -e.y, -e.z, 1},
        {e.x, e.y, -e.z, 1},
        {-e.x, e.y, -e.z, 1},
        {-e.x, -e.y, e.z, 1},
        {e.x, -e.y, e.z, 1},
        {e.x, e.y, e.z, 1},
        {-e.x, e.y, e.z, 1},
    };
    glm::vec3 w[8];
    for (int i = 0; i < 8; ++i)
        w[i] = glm::vec3(transform * corners[i]);

    // 12 edges
    const int edges[12][2] = {
        {0, 1},
        {1, 2},
        {2, 3},
        {3, 0}, // back face
        {4, 5},
        {5, 6},
        {6, 7},
        {7, 4}, // front face
        {0, 4},
        {1, 5},
        {2, 6},
        {3, 7}, // connecting edges
    };

    std::vector<Vertex> v;
    v.reserve(24);
    for (auto &e2 : edges)
        addLine(v, w[e2[0]], w[e2[1]], color);

    pushShape(std::move(v), lifetime, depthTest);
}

void DebugDraw::sphere(glm::vec3 center, float radius, glm::vec4 color, float lifetime, bool depthTest, int segments)
{
    std::vector<Vertex> v;
    v.reserve(segments * 6);

    auto circle = [&](glm::vec3 (*fn)(float))
    {
        for (int i = 0; i < segments; ++i)
        {
            float a0 = glm::two_pi<float>() * float(i) / float(segments);
            float a1 = glm::two_pi<float>() * float(i + 1) / float(segments);
            addLine(v, center + fn(a0) * radius, center + fn(a1) * radius, color);
        }
    };

    circle([](float a) -> glm::vec3
           { return {std::cos(a), 0.0f, std::sin(a)}; }); // XZ
    circle([](float a) -> glm::vec3
           { return {std::cos(a), std::sin(a), 0.0f}; }); // XY
    circle([](float a) -> glm::vec3
           { return {0.0f, std::sin(a), std::cos(a)}; }); // YZ

    pushShape(std::move(v), lifetime, depthTest);
}

void DebugDraw::frustum(glm::mat4 invViewProj, glm::vec4 color, float lifetime)
{
    // 8 NDC corners → world space
    glm::vec4 ndc[8] = {
        {-1, -1, 0, 1},
        {1, -1, 0, 1},
        {1, 1, 0, 1},
        {-1, 1, 0, 1}, // near
        {-1, -1, 1, 1},
        {1, -1, 1, 1},
        {1, 1, 1, 1},
        {-1, 1, 1, 1}, // far
    };
    glm::vec3 w[8];
    for (int i = 0; i < 8; ++i)
    {
        glm::vec4 p = invViewProj * ndc[i];
        w[i] = glm::vec3(p) / p.w;
    }

    std::vector<Vertex> v;
    v.reserve(24);
    // near quad
    addLine(v, w[0], w[1], color);
    addLine(v, w[1], w[2], color);
    addLine(v, w[2], w[3], color);
    addLine(v, w[3], w[0], color);
    // far quad
    addLine(v, w[4], w[5], color);
    addLine(v, w[5], w[6], color);
    addLine(v, w[6], w[7], color);
    addLine(v, w[7], w[4], color);
    // connecting edges
    addLine(v, w[0], w[4], color);
    addLine(v, w[1], w[5], color);
    addLine(v, w[2], w[6], color);
    addLine(v, w[3], w[7], color);

    pushShape(std::move(v), lifetime, true);
}

void DebugDraw::raycast(glm::vec3 origin, glm::vec3 direction, float length, glm::vec4 color, float lifetime)
{
    glm::vec3 end = origin + glm::normalize(direction) * length;
    std::vector<Vertex> v;
    v.reserve(2);
    addLine(v, origin, end, color);
    // small cross at the hit end
    float cs = length * 0.03f;
    glm::vec3 rx = {cs, 0, 0}, ry = {0, cs, 0}, rz = {0, 0, cs};
    addLine(v, end - rx, end + rx, color);
    addLine(v, end - ry, end + ry, color);
    addLine(v, end - rz, end + rz, color);
    pushShape(std::move(v), lifetime, true);
}

void DebugDraw::capsule(glm::vec3 base, glm::vec3 tip, float radius, glm::vec4 color, float lifetime)
{
    // Draw as sphere at base, sphere at tip, and 4 connecting lines
    sphere(base, radius, color, lifetime, true, 12);
    sphere(tip, radius, color, lifetime, true, 12);

    glm::vec3 axis = glm::normalize(tip - base);
    // Build a perpendicular basis
    glm::vec3 perp = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 r1 = glm::normalize(glm::cross(axis, perp)) * radius;
    glm::vec3 r2 = glm::normalize(glm::cross(axis, r1)) * radius;

    std::vector<Vertex> v;
    v.reserve(8);
    addLine(v, base + r1, tip + r1, color);
    addLine(v, base - r1, tip - r1, color);
    addLine(v, base + r2, tip + r2, color);
    addLine(v, base - r2, tip - r2, color);
    pushShape(std::move(v), lifetime, true);
}

void DebugDraw::cone(glm::vec3 apex, glm::vec3 direction, float length,
                     float halfAngleDeg, glm::vec4 color, float lifetime, int segments)
{
    glm::vec3 dir = glm::normalize(direction);
    glm::vec3 base = apex + dir * length;
    float r = length * std::tan(glm::radians(halfAngleDeg));

    glm::vec3 perp = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 u = glm::normalize(glm::cross(dir, perp));
    glm::vec3 v2 = glm::normalize(glm::cross(dir, u));

    std::vector<Vertex> v;
    v.reserve(segments * 4);

    for (int i = 0; i < segments; ++i)
    {
        float a0 = glm::two_pi<float>() * float(i) / float(segments);
        float a1 = glm::two_pi<float>() * float(i + 1) / float(segments);
        glm::vec3 p0 = base + (u * std::cos(a0) + v2 * std::sin(a0)) * r;
        glm::vec3 p1 = base + (u * std::cos(a1) + v2 * std::sin(a1)) * r;
        addLine(v, p0, p1, color); // circle at base
        if (i % (segments / 4) == 0)
            addLine(v, apex, p0, color); // 4 lines from apex
    }

    pushShape(std::move(v), lifetime, true);
}

void DebugDraw::aabb(glm::vec3 mn, glm::vec3 mx, glm::vec4 color, float lifetime)
{
    glm::vec3 center = (mn + mx) * 0.5f;
    glm::vec3 half = (mx - mn) * 0.5f;
    box(glm::translate(glm::mat4(1.0f), center), half, color, lifetime, true);
}

void DebugDraw::cross(glm::vec3 center, float size, float lifetime)
{
    std::vector<Vertex> v;
    v.reserve(6);
    addLine(v, center - glm::vec3(size, 0, 0), center + glm::vec3(size, 0, 0), {1, 0, 0, 1}); // X red
    addLine(v, center - glm::vec3(0, size, 0), center + glm::vec3(0, size, 0), {0, 1, 0, 1}); // Y green
    addLine(v, center - glm::vec3(0, 0, size), center + glm::vec3(0, 0, size), {0, 0, 1, 1}); // Z blue
    pushShape(std::move(v), lifetime, true);
}

void DebugDraw::flush(float deltaTime)
{
    std::lock_guard<std::mutex> lock(mutex());
    auto &s = shapes();
    // Remove zero-lifetime shapes (drawn last frame) and decay positive lifetimes
    for (auto it = s.begin(); it != s.end();)
    {
        if (it->lifetime <= 0.0f)
            it = s.erase(it);
        else
        {
            it->lifetime -= deltaTime;
            ++it;
        }
    }
}

void DebugDraw::collectVertices(std::vector<Vertex> &out)
{
    std::lock_guard<std::mutex> lock(mutex());
    for (auto &shape : shapes())
        for (const auto &vert : shape.vertices)
            out.push_back(vert);
}

ELIX_NESTED_NAMESPACE_END
