#include "DebugLine.hpp"

#include <glad.h>
#include <glm/gtc/type_ptr.hpp>

#include "CameraManager.hpp"
#include "ElixirCore/Filesystem.hpp"
#include "Renderer.hpp"
#include "ElixirCore/WindowsManager.hpp"

debug::DebugLine::DebugLine()
{
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    const std::string path = filesystem::getShadersFolderPath().string();

    m_shader.load(path + "/line.vert", path + "/line.frag");
}

void debug::DebugLine::draw(const glm::vec3 &from, const glm::vec3 &to)
{
    glLineWidth(m_lineWidth);

    const float vertices[] = {
        from.x, from.y, from.z,
        to.x, to.y, to.z
    };

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    m_shader.bind();

    const auto& frameData = Renderer::instance().getFrameData();

    m_shader.setMat4("projection", frameData.projectionMatrix);
    m_shader.setMat4("view", frameData.viewMatrix);
    m_shader.setVec4("uColor", m_color);

    glDrawArrays(GL_LINES, 0, 2);

    glBindVertexArray(0);
}

void debug::DebugLine::setLineWidth(float width)
{
    m_lineWidth = width;
}

void debug::DebugLine::setColor(const glm::vec4 &color)
{
    m_color = color;
}
