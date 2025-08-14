#ifndef STENCIL_RENDER_HPP
#define STENCIL_RENDER_HPP

#include <VelixFlow/RenderAPI/Interface/IRenderPass.hpp>
#include <VelixFlow/GameObject.hpp>


class StencilRender : public elix::render::IRenderPass
{
public:
    bool shouldExecute() const;
    std::string getName() const;
    void render(const elix::render::FrameData& frameData, elix::Scene* scene = nullptr);
    int getPriority() const;

    void setSelectedGameObject(elix::GameObject* gameObject);

private:
    elix::GameObject* m_selectedGameObject{nullptr};
};


#endif //STENCIL_RENDER_HPP