#ifndef DELETEACTION_HPP
#define DELETEACTION_HPP

#include "Action.hpp"
#include "VelixFlow/GameObject.hpp"


class DeleteAction final : public Action
{
public:
  explicit DeleteAction(GameObject* gameObject);

  void execute() override;

  void undo() override;

  void redo() override;

  ~DeleteAction() override;
private:
  GameObject* m_gameObject{nullptr};
  std::shared_ptr<GameObject> m_undoObject{nullptr};
};

#endif //DELETEACTION_HPP
