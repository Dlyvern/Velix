#include "ActionsManager.hpp"

void ActionsManager::execute(std::unique_ptr<Action> action)
{
    action->execute();

    m_undoActions.push_back(std::move(action));
}

void ActionsManager::undo()
{
    if (m_undoActions.empty())
        return;

    auto action = std::move(m_undoActions.back());

    action->undo();

    m_undoActions.pop_back();
}

void ActionsManager::redo()
{
}
