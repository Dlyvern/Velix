#ifndef ELIX_ENTITY_DETAILS_VIEW_HPP
#define ELIX_ENTITY_DETAILS_VIEW_HPP

#include "Editor/Panels/DetailsViews/IDetailsView.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class EntityDetailsView final : public IDetailsView
{
public:
    bool canDraw(const Editor &editor) const override;
    void draw(Editor &editor) override;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ENTITY_DETAILS_VIEW_HPP
