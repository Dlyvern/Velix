#ifndef ELIX_COMPONENT_REGISTRY_HPP
#define ELIX_COMPONENT_REGISTRY_HPP

#include "Core/Macros.hpp"

#include <functional>
#include <string>
#include <vector>

// Forward declarations
namespace elix::engine
{
class Entity;
class Scene;
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

// Context passed to the component add function.
// Provides notification callbacks and an output flag to control popup behaviour.
struct ComponentAddContext
{
    // Notification callbacks — may be null (e.g. when called from non-editor context).
    std::function<void(const std::string &)> showSuccess;
    std::function<void(const std::string &)> showWarning;
    std::function<void(const std::string &)> showError;

    // The add function can set this to false to keep the "Add Component" popup open
    // (e.g. when a warning is shown instead of adding the component).
    bool closePopup{true};
};

struct ComponentEntry
{
    std::string displayName;
    std::string category; // e.g. "Common", "Physics", "Rendering", "Plugin"
    std::function<void(Entity *, Scene *, ComponentAddContext &)> addFn;
};

// Central registry of addable components.
// Engine built-ins are registered at editor startup.
// Plugins can register additional entries via onLoad().
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

#endif // ELIX_COMPONENT_REGISTRY_HPP
