#include "DeleteAction.hpp"

#include "ElixirCore/SceneManager.hpp"

DeleteAction::DeleteAction(GameObject *gameObject) : m_gameObject(gameObject)
{
    auto newGameObject = std::make_shared<GameObject>(*m_gameObject);
    m_undoObject = newGameObject;
}

void DeleteAction::execute()
{
    SceneManager::instance().getCurrentScene()->deleteGameObject(m_gameObject);
}

void DeleteAction::undo()
{
}

void DeleteAction::redo()
{
}

DeleteAction::~DeleteAction() = default;