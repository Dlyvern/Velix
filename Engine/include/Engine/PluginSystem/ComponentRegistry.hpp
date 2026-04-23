#ifndef ELIX_COMPONENT_REGISTRY_HPP
#define ELIX_COMPONENT_REGISTRY_HPP

#include "Core/Macros.hpp"

#include <functional>
#include <string>
#include <vector>


namespace elix::engine
{
class Entity;
class Scene;
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)



struct ComponentAddContext
{

    std::function<void(const std::string &)> showSuccess;
    std::function<void(const std::string &)> showWarning;
    std::function<void(const std::string &)> showError;



    bool closePopup{true};
};

struct ComponentEntry
{
    std::string displayName;
    std::string category;
    std::function<void(Entity *, Scene *, ComponentAddContext &)> addFn;
};




class ComponentRegistry
{
public:
    static ComponentRegistry &instance();

    void registerComponent(std::string displayName,
                           std::string category,
                           std::function<void(Entity *, Scene *, ComponentAddContext &)> addFn);

    const std::vector<ComponentEntry> &getEntries() const;

    void clear();

private:
    ComponentRegistry() = default;

    std::vector<ComponentEntry> m_entries;
};

ELIX_NESTED_NAMESPACE_END

#endif
