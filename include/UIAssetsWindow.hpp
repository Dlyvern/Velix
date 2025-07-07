#ifndef UI_ASSETS_WINDOW_HPP
#define UI_ASSETS_WINDOW_HPP

#include "EditorCommon.hpp"
#include <filesystem>

namespace elixUI
{
    class UIAssetsWindow
    {
    public:
        void draw();
        
        std::string getFileEditorPath() const
        {
            return m_fileEditorPath;
        }
    private:
        editorCommon::DraggingInfo m_draggingInfo;
        std::filesystem::path m_assetsPath;
        std::string m_fileEditorPath;
    };
}

#endif //UI_ASSETS_WINDOW_HPP