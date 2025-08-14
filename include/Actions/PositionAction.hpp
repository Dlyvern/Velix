#ifndef POSITION_ACTION_HPP
#define POSITION_ACTION_HPP

#include "Action.hpp"
#include "VelixFlow/GameObject.hpp"

class PositionAction final : public Action
{
public:
    PositionAction(elix::GameObject* gameObject, const glm::vec3& oldPosition, const glm::vec3& newPosition);

    void execute() override;

    void undo() override;

    void redo() override;

    ~PositionAction() override;
 private:
    elix::GameObject* m_gameObject{nullptr};
    glm::vec3 m_oldPosition;
    glm::vec3 m_newPosition;
};

#endif //POSITION_ACTION_HPP
