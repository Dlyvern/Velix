#ifndef UI_MESH_HPP
#define UI_MESH_HPP

#include <VelixFlow/Mesh.hpp>

class GameObject;

class UIMesh
{
public:
    // static void draw(common::Mesh* mesh, int meshIndex, GameObject* gameObject);
    static void draw(elix::Mesh* mesh, int meshIndex, GameObject* gameObject);

};

#endif //UI_MESH_HPP
