#ifndef ELIX_TRANSFORM_2D_HPP
#define ELIX_TRANSFORM_2D_HPP

#include "Core/Macros.hpp"
#include "Engine/Components/ECS.hpp"

#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtc/matrix_transform.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Transform2DComponent : public ECS
{
public:
    void setPosition(const glm::vec2& position);
    void setScale(const glm::vec2& scale);
    void setRotation(float rotation);

    const glm::vec2& getPosition() const;
    const glm::vec2& getScale() const;
    float getRotation() const;
    glm::mat4 getMatrix() const;
private:
    glm::vec2 m_position{0.0f, 0.0f};
    glm::vec2 m_scale{1.0f};
    float m_rotation{0.0f};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_TRANSFORM_2D_HPP