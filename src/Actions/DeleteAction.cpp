#include "Actions/DeleteAction.hpp"

DeleteAction::DeleteAction(elix::GameObject *gameObject) : m_gameObject(gameObject)
{
    auto newGameObject = std::make_shared<elix::GameObject>(*m_gameObject);
    m_undoObject = newGameObject;
}

void DeleteAction::execute()
{
    // SceneManager::instance().getCurrentScene()->deleteGameObject(m_gameObject);
}

void DeleteAction::undo()
{
}

void DeleteAction::redo()
{
}

DeleteAction::~DeleteAction() = default;