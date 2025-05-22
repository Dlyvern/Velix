#include "PositionAction.hpp"

PositionAction::PositionAction(GameObject *gameObject, const glm::vec3 &oldPosition, const glm::vec3 &newPosition) : m_gameObject(gameObject),
m_oldPosition(oldPosition), m_newPosition(newPosition)
{
}

void PositionAction::execute()
{
    m_gameObject->setPosition(m_newPosition);
}

void PositionAction::undo()
{
    m_gameObject->setPosition(m_oldPosition);
}

void PositionAction::redo()
{
}

PositionAction::~PositionAction() = default;
