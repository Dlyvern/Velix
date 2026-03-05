#ifndef ELIX_SCENE_HPP
#define ELIX_SCENE_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/Lights.hpp"

#include "Engine/Physics/PhysicsScene.hpp"

#include "Engine/UI/UIText.hpp"
#include "Engine/UI/UIButton.hpp"
#include "Engine/UI/Billboard.hpp"

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Scene
{
public:
    using SharedPtr = std::shared_ptr<Scene>;
    using LoadStatusCallback = std::function<void(const std::string &)>;
    Scene();
    ~Scene();

    Scene::SharedPtr copy();

    const std::vector<Entity::SharedPtr> &getEntities() const;

    std::vector<std::shared_ptr<BaseLight>> getLights();

    bool doesEntityNameExist(const std::string &name) const;

    Entity::SharedPtr
    addEntity(const std::string &name);
    Entity::SharedPtr addEntity(Entity &en, const std::string &name);

    Entity *getEntityById(uint32_t id);

    bool destroyEntity(Entity *entity);

    bool loadSceneFromFile(const std::string &filePath, const LoadStatusCallback &statusCallback = {});
    void saveSceneToFile(const std::string &filePath);

    void setSkyboxHDRPath(const std::string &path);
    const std::string &getSkyboxHDRPath() const;
    bool hasSkyboxHDR() const;
    void clearSkyboxHDR();

    void update(float deltaTime);
    void fixedUpdate(float fixedDelta);

    PhysicsScene &getPhysicsScene();

    // --- UI game objects ---
    ui::UIText   *addUIText();
    ui::UIButton *addUIButton();
    ui::Billboard *addBillboard();

    void removeUIText(const ui::UIText *text);
    void removeUIButton(const ui::UIButton *button);
    void removeBillboard(const ui::Billboard *billboard);

    const std::vector<std::unique_ptr<ui::UIText>>    &getUITexts()    const;
    const std::vector<std::unique_ptr<ui::UIButton>>  &getUIButtons()  const;
    const std::vector<std::unique_ptr<ui::Billboard>> &getBillboards() const;

private:
    std::vector<Entity::SharedPtr> m_entities;
    std::string m_name;
    PhysicsScene m_physicsScene;
    uint32_t m_nextEntityId{0};
    std::string m_skyboxHDRPath;

    std::vector<std::unique_ptr<ui::UIText>>    m_uiTexts;
    std::vector<std::unique_ptr<ui::UIButton>>  m_uiButtons;
    std::vector<std::unique_ptr<ui::Billboard>> m_billboards;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCENE_HPP
