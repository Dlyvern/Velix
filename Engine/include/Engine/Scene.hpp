#ifndef ELIX_SCENE_HPP
#define ELIX_SCENE_HPP

#include "Core/Macros.hpp"

#include "Engine/Entity.hpp"
#include "Engine/EnvironmentSettings.hpp"
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
    Entity::SharedPtr addEntityWithId(const std::string &name, uint32_t id);
    Entity::SharedPtr addEntity(Entity &en, const std::string &name);

    Entity *getEntityById(uint32_t id);

    bool destroyEntity(Entity *entity);

    bool loadSceneFromFile(const std::string &filePath, const LoadStatusCallback &statusCallback = {}, bool additive = false);
    bool loadEntitiesFromFile(const std::string &filePath, const LoadStatusCallback &statusCallback = {});
    void saveSceneToFile(const std::string &filePath);
    bool serializeEntityHierarchy(uint32_t rootEntityId, std::string &outPayload) const;
    Entity *restoreEntityHierarchy(const std::string &payload, uint32_t *outRootEntityId = nullptr);
    void serializeUIState(std::string &outPayload) const;
    bool restoreUIState(const std::string &payload);

    // Extract all entities that have a specific tag, removing them from this scene.
    std::vector<Entity::SharedPtr> extractEntitiesWithTag(const std::string &tag);

    // Inject pre-existing entity shared_ptrs directly (used by SceneManager for DontDestroyOnLoad).
    void injectEntities(std::vector<Entity::SharedPtr> entities);

    void setSkyboxHDRPath(const std::string &path);
    const std::string &getSkyboxHDRPath() const;
    bool hasSkyboxHDR() const;
    void clearSkyboxHDR();
    const FogSettings &getFogSettings() const;
    FogSettings &getFogSettings();
    void setFogSettings(const FogSettings &settings);
    const SceneEnvironmentSettings &getEnvironmentSettings() const;
    SceneEnvironmentSettings &getEnvironmentSettings();
    void setEnvironmentSettings(const SceneEnvironmentSettings &settings);

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
    SceneEnvironmentSettings m_environmentSettings{};

    std::vector<std::unique_ptr<ui::UIText>>    m_uiTexts;
    std::vector<std::unique_ptr<ui::UIButton>>  m_uiButtons;
    std::vector<std::unique_ptr<ui::Billboard>> m_billboards;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCENE_HPP
