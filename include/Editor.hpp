#ifndef DEBUG_EDITOR_HPP
#define DEBUG_EDITOR_HPP

#include "ElixirCore/GameObject.hpp"
#include "ElixirCore/Skeleton.hpp"
#include <filesystem>
#include "ElixirCore/Filesystem.hpp"
#include "ActionsManager.hpp"

class Editor
{
public:
    static Editor& instance();
    void update();
    ~Editor();
private:

    void* m_gameLibrary{nullptr};



    struct DraggingInfo final
    {
    public:
        std::string name;
    };

    void showViewPort();
    void showDebugInfo();
    void showObjectInfo();
    void showProperties();
    void showMaterialInfo();
    void showAllObjectsInTheScene();
    void showAssetsInfo();
    void showGuizmosInfo();

    void displayBonesHierarchy(Skeleton* skeleton, common::BoneInfo* parent = nullptr);

    void updateInput();

    std::filesystem::path m_assetsPath{filesystem::getResourcesFolderPath()};

    Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) = delete;
    Editor& operator=(Editor&&) = delete;

    DraggingInfo m_draggingInfo;

    GameObject* m_selectedGameObject{nullptr};
    Material* m_selectedMaterial{nullptr};

    ActionsManager m_actionsManager;

    std::shared_ptr<GameObject> m_savedGameObject{nullptr};

    std::vector<GameObject*> m_selectedGameObjects;

    enum class TransformMode
    {
        Translate,
        Rotate,
        Scale
    };

    TransformMode m_transformMode{TransformMode::Translate};

    int m_selectedModelIndex{-1};
};

#endif //DEBUG_EDITOR_HPP
