#ifndef ELIX_I_DETAILS_VIEW_HPP
#define ELIX_I_DETAILS_VIEW_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class Editor;

class IDetailsView
{
public:
    virtual ~IDetailsView() = default;

    virtual bool canDraw(const Editor &editor) const = 0;
    virtual void draw(Editor &editor) = 0;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_I_DETAILS_VIEW_HPP
