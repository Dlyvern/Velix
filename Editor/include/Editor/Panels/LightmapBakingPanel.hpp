#ifndef ELIX_LIGHTMAP_BAKING_PANEL_HPP
#define ELIX_LIGHTMAP_BAKING_PANEL_HPP

#include "Core/Macros.hpp"
#include "Engine/Render/LightmapBaker.hpp"

#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class LightmapBakingPanel
{
public:
    using BakeCallback = std::function<void(const engine::LightmapBakeSettings &)>;

    void draw(bool *open);

    void setOnBakeRequested(BakeCallback callback)
    {
        m_onBakeRequested = std::move(callback);
    }

    void setProgress(engine::LightmapBakeProgress *progress)
    {
        m_progress = progress;
    }

private:
    BakeCallback                   m_onBakeRequested;
    engine::LightmapBakeProgress  *m_progress{nullptr};

    // Settings state
    int  m_resolution{1024};
    int  m_dilateRadius{2};
    bool m_dilate{true};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_LIGHTMAP_BAKING_PANEL_HPP
