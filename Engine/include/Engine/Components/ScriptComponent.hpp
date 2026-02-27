#ifndef ELIX_SCRIPT_COMPONENT_HPP
#define ELIX_SCRIPT_COMPONENT_HPP

#include "Engine/Components/ECS.hpp"
#include "Engine/Scripting/Script.hpp"

#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ScriptComponent : public ECS
{
public:
    explicit ScriptComponent(Script *script);
    ScriptComponent(const std::string &scriptName, Script *script);

    void onAttach() override;

    void update(float deltaTime) override;

    void onDetach() override;

    [[nodiscard]] const std::string &getScriptName() const;
    [[nodiscard]] Script *getScript() const;

    ~ScriptComponent() override;

protected:
    void onOwnerAttached() override;

private:
    Script *m_script{nullptr};
    std::string m_scriptName;
    bool m_isAttached{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCRIPT_COMPONENT_HPP
