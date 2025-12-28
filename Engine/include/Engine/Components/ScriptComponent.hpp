#ifndef ELIX_SCRIPT_COMPONENT_HPP
#define ELIX_SCRIPT_COMPONENT_HPP

#include "Engine/Components/ECS.hpp"
#include "Engine/Scripting/Script.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ScriptComponent : public ECS
{
public:
    ScriptComponent(Script* script);

    void onAttach() override;

    void update(float deltaTime) override;

    void onDetach() override;
private:
    Script* m_script{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SCRIPT_COMPONENT_HPP