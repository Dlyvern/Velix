#ifndef ACTIONS_MANAGER_HPP
#define ACTIONS_MANAGER_HPP

#include <memory>
#include <vector>
#include "Action.hpp"

class ActionsManager
{
public:
    void execute(std::unique_ptr<Action> action);
    void undo();
    void redo();

private:
    std::vector<std::unique_ptr<Action>> m_undoActions;
    std::vector<std::unique_ptr<Action>> m_redoActions;
};

#endif //ACTIONS_MANAGER_HPP
