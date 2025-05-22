#ifndef DEBUG_LINE_HPP
#define DEBUG_LINE_HPP

#include "Shader.hpp"
namespace debug
{
class DebugLine
{
public:
    DebugLine();

    void draw(const glm::vec3& from, const glm::vec3& to);

    void setLineWidth(float width);
    void setColor(const glm::vec4& color);

private:
    unsigned int m_vao;
    unsigned int m_vbo;

    float m_lineWidth{2.0f};

    glm::vec4 m_color{0, 1, 0, 1};

    GLitch::Shader m_shader;
};
} //namespace debug

#endif //DEBUG_LINE_HPP
