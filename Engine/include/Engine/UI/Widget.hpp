#ifndef ELIX_WIDGET_HPP
#define ELIX_WIDGET_HPP

#include "Core/Macros.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

class Widget
{
public:
    void setParent(Widget *parent);
    void removeParent();

    virtual void update(float deltaTime) {}

    virtual ~Widget();

private:
    Widget *m_parent{nullptr};
    std::vector<Widget *> m_children;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_WIDGET_HPP