#include "Engine/PluginSystem/ComponentRegistry.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

ComponentRegistry &ComponentRegistry::instance()
{
    static ComponentRegistry s_instance;
    return s_instance;
}

void ComponentRegistry::registerComponent(std::string displayName,
                                          std::string category,
                                          std::function<void(Entity *, Scene *, ComponentAddContext &)> addFn)
{
    m_entries.push_back({std::move(displayName), std::move(category), std::move(addFn)});
}

const std::vector<ComponentEntry> &ComponentRegistry::getEntries() const
{
    return m_entries;
}

void ComponentRegistry::clear()
{
    m_entries.clear();
}

ELIX_NESTED_NAMESPACE_END
