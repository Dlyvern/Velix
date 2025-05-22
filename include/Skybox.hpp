#ifndef SKYBOX_HPP
#define SKYBOX_HPP

#include <vector>
#include "Shader.hpp"

class Skybox
{
public:
    explicit Skybox(const std::vector<std::string>& faces);

    Skybox();

    void init(const std::vector<std::string>& faces);

    void render();

    void loadFromHDR(const std::string& path);
private:
    GLitch::Shader m_skyboxShader;
    unsigned int m_vao, m_vbo, m_ebo;
    unsigned int m_cubeMapTextureId;

    static unsigned int loadCubemap(const std::vector<std::string>& faces);
};

#endif //SKYBOX_HPP
