#ifndef ELIX_UI_RENDER_DATA_HPP
#define ELIX_UI_RENDER_DATA_HPP

#include "Engine/UI/UIText.hpp"
#include "Engine/UI/UIButton.hpp"
#include "Engine/UI/Billboard.hpp"

#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(ui)

/// Transient per-frame snapshot of all UI game objects to render.
/// Assembled by the render graph orchestrator (e.g. EditorRuntime) from the
/// scene's UI collections, then handed to UIRenderGraphPass before record().
struct UIRenderData
{
    std::vector<const UIText *>     texts;
    std::vector<const UIButton *>   buttons;
    std::vector<const Billboard *>  billboards;
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_UI_RENDER_DATA_HPP
