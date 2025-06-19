#ifndef DEBUG_EDITOR_HPP
#define DEBUG_EDITOR_HPP

#include "ElixirCore/GameObject.hpp"
#include "ElixirCore/Skeleton.hpp"
#include <filesystem>
#include "ElixirCore/Filesystem.hpp"
#include "ActionsManager.hpp"
#include "Camera.hpp"

class Editor
{

public:
    enum class State
    {
        Start,
        Editor,
        Play
    };

    static Editor& instance();
    void update();
    [[nodiscard]] Editor::State getState() const;
    ~Editor();
    Camera* m_editorCamera{nullptr};

private:


    State m_state{State::Start};

    void* m_gameLibrary{nullptr};

    struct DraggingInfo final
    {
    public:
        std::string name;
    };

    void showStart();

    void showEditor();

    void showMenuBar();

    void showViewPort();
    void showDebugInfo();
    void showObjectInfo();
    void showProperties();
    void showAllObjectsInTheScene();
    void showAssetsInfo();
    void showGuizmosInfo();
	
    void showGuizmo(GameObject* gameObject, float x, float y, float width, float height); 

    void drawLogWindow();

    void drawTerminal();

    void setSelectedGameObject(GameObject* gameObject);

    void displayBonesHierarchy(Skeleton* skeleton, common::BoneInfo* parent = nullptr);

    void updateInput();

    std::filesystem::path m_assetsPath{};

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
