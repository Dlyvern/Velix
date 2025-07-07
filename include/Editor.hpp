#ifndef DEBUG_EDITOR_HPP
#define DEBUG_EDITOR_HPP

#include <VelixFlow/GameObject.hpp>
#include <filesystem>

#include "ActionsManager.hpp"
#include "Camera.hpp"
#include "IInspectable.hpp"

#include "UILogger.hpp"
#include "UITerminal.hpp"
#include "UIAssetsWindow.hpp"
    
class Editor
{
public:
    enum class State
    {
        Start,
        Editor,
        Play
    };

    Editor();
    
    void init();
    void update(float deltaTime);
    [[nodiscard]] Editor::State getState() const;
    ~Editor();
    void destroy();

private:
    elixUI::UILogger m_uiLogger;
    elixUI::UITerminal m_uiTerminal;
    elixUI::UIAssetsWindow m_uiAssetsWindow;
    
    State m_state{State::Start};

    std::unique_ptr<Camera> m_editorCamera{nullptr};
    
    void showStart();

    void drawMainScene();

    void showTextEditor();

    void showEditor();

    void showMenuBar();

    void showViewPort();
    void showDebugInfo();
    void showProperties();
    void showAllObjectsInTheScene();
    void showAssetsInfo();
	
    void showGuizmo(GameObject* gameObject, float x, float y, float width, float height); 

    void drawLogWindow();

    void drawTerminal();

    void setSelectedGameObject(GameObject* gameObject);

    void updateInput();

    std::shared_ptr<IInspectable> m_selected;

    GameObject* m_selectedGameObject{nullptr};

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
};

#endif //DEBUG_EDITOR_HPP
