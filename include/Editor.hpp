#ifndef EDITOR_HPP
#define EDITOR_HPP

#include <VelixFlow/Scene.hpp>
#include <VelixFlow/UI/UIWidget.hpp>
#include <VelixFlow/AssetsCache.hpp>

#include <memory>

class Editor
{
public:
    void init();

    void update(float deltaTime);

    std::shared_ptr<elix::Scene> getOverlay();
private:
    enum class State
    {
        MAIN_MENU,
        EDITOR
    };

    void addUI(const std::shared_ptr<elix::ui::UIWidget>& element);
    std::shared_ptr<elix::Scene> m_overlay;
    elix::AssetsCache m_editorCache;
    State m_state{State::MAIN_MENU};
};


#endif //EDITOR_HPP