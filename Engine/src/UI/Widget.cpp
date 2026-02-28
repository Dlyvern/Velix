#include "Engine/UI/Widget.hpp"

#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

void Widget::setParent(Widget *parent)
{
    m_parent = parent;
    parent->m_children.push_back(this);
}

void Widget::removeParent()
{
    if (!m_parent)
        return;

    auto &children = m_parent->m_children;

    auto it = std::find_if(children.begin(), children.end(), [this](Widget *child)
                           { return child == this; });

    if (it != children.end())
        children.erase(it);
}

Widget::~Widget() = default;

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END