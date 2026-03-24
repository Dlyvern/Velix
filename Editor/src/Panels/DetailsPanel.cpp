#include "Editor/Panels/DetailsPanel.hpp"

#include "Editor/Panels/DetailsViews/AssetDetailsView.hpp"
#include "Editor/Panels/DetailsViews/EntityDetailsView.hpp"

#include <imgui.h>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

DetailsPanel::DetailsPanel()
{
    m_views.emplace_back(std::make_unique<AssetDetailsView>());
    m_views.emplace_back(std::make_unique<EntityDetailsView>());
}

DetailsPanel::~DetailsPanel() = default;

void DetailsPanel::drawContents(Editor &editor)
{
    for (const auto &view : m_views)
    {
        if (!view->canDraw(editor))
            continue;

        view->draw(editor);
        return;
    }

    ImGui::TextUnformatted("Select an object or asset to view details");
}

ELIX_NESTED_NAMESPACE_END
