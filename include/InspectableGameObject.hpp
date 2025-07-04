#ifndef INSPECTABLE_GAME_OBJECT_HPP
#define INSPECTABLE_GAME_OBJECT_HPP

#include "IInspectable.hpp"
#include <memory>

class GameObject;

class InspectableGameObject : public IInspectable
{
public:
    explicit InspectableGameObject(GameObject* gameObject);
    void draw() override;
private:
    GameObject* m_gameObject{nullptr};
};

#endif //INSPECTABLE_GAME_OBJECT_HPP