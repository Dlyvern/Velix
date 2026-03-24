#ifndef ELIX_DETAILS_PANEL_HPP
#define ELIX_DETAILS_PANEL_HPP

#include "Core/Macros.hpp"
#include "Editor/Panels/DetailsViews/IDetailsView.hpp"

#include <memory>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class Editor;

class DetailsPanel
{
public:
    DetailsPanel();
    ~DetailsPanel();

    void drawContents(Editor &editor);

private:
    std::vector<std::unique_ptr<IDetailsView>> m_views;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_DETAILS_PANEL_HPP
