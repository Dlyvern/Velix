#ifndef STENCIL_RENDER_HPP
#define STENCIL_RENDER_HPP

#include <VelixFlow/Render.hpp>

class StencilRender : public elix::IRender
{
public:
    bool shouldExecute() const;
    std::string getName() const;
    void render(const elix::FrameData& frameData, Scene* scene = nullptr);
    int getPriority() const;

    void setSelectedGameObject(GameObject* gameObject);

private:
    GameObject* m_selectedGameObject{nullptr};
};


#endif //STENCIL_RENDER_HPP