#ifndef ELIX_ASSETS_WINDOW_HPP
#define ELIX_ASSETS_WINDOW_HPP

#include "Core/Macros.hpp"

#include "Editor/Project.hpp"

#include "Editor/EditorResourcesStorage.hpp"

#include "Engine/Material.hpp"

#include "Editor/AssetsPreviewSystem.hpp"

#include <string>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class AssetsWindow
{
public:
    AssetsWindow(EditorResourcesStorage *resourcesStorage, AssetsPreviewSystem &assetsPreviewSystem);

    void setProject(Project *project);

    void draw();

    void setOnMaterialOpenRequest(const std::function<void(const std::filesystem::path &)> &function)
    {
        m_onMaterialOpenRequestFunction = function;
    }

    void setOnTextAssetOpenRequest(const std::function<void(const std::filesystem::path &)> &function)
    {
        m_onTextAssetOpenRequestFunction = function;
    }

    void setOnAssetSelectionChanged(const std::function<void(const std::filesystem::path &)> &function)
    {
        m_onAssetSelectionChangedFunction = function;
    }

private:
    std::function<void(const std::filesystem::path &)> m_onMaterialOpenRequestFunction{nullptr};
    std::function<void(const std::filesystem::path &)> m_onTextAssetOpenRequestFunction{nullptr};
    std::function<void(const std::filesystem::path &)> m_onAssetSelectionChangedFunction{nullptr};

    AssetsPreviewSystem &m_assetsPreviewSystem;

    struct TreeNode
    {
        std::string name;
        std::filesystem::path path;
        bool isOpen = false;
        std::vector<std::shared_ptr<TreeNode>> children;
        TreeNode *parent = nullptr;

        TreeNode(const std::string &n, const std::filesystem::path &p) : name(n), path(p) {}
    };

    void drawSearchBar();
    void drawTreeView();
    void drawAssetGrid();
    std::vector<std::filesystem::directory_entry> getFilteredEntries();

    void buildDirectoryTree();
    void buildTreeNode(TreeNode *node, const std::filesystem::path &path);
    void syncTreeWithCurrentDirectory();
    void goUpOneDirectory();
    void refreshTree();
    void startRenamingAsset(const std::filesystem::path &path);
    void startDeletingAsset(const std::filesystem::path &path);
    bool renameAsset(const std::filesystem::path &path, const std::string &newName);
    bool deleteAsset(const std::filesystem::path &path);
    bool duplicateAsset(const std::filesystem::path &path);
    bool createMaterialFromTexture(const std::filesystem::path &texturePath);
    void setSelectedAssetPath(const std::filesystem::path &path);

    void navigateToDirectory(const std::filesystem::path &path);
    std::string formatFileSize(uintmax_t size) const;

    bool matchesSearch(const std::string &filename) const;
    void refreshCurrentDirectory();

    Project *m_currentProject{nullptr};
    EditorResourcesStorage *m_resourcesStorage{nullptr};
    std::filesystem::path m_currentDirectory;
    char m_searchBuffer[256] = "";
    std::string m_searchQuery;

    std::unordered_set<std::string> m_excludedDirectories = {
        "build", "cmake-build-debug", "cmake-build-release",
        ".git", ".vs", ".vscode", "node_modules",
        "__pycache__", ".idea", "out", "bin", "obj",
        "Debug", "Release", "x64", "x86", "temp"};

    std::shared_ptr<TreeNode> m_treeRoot;
    TreeNode *m_selectedTreeNode = nullptr;
    std::filesystem::path m_selectedAssetPath;
    std::filesystem::path m_contextAssetPath;
    std::filesystem::path m_renameAssetPath;
    std::filesystem::path m_deleteAssetPath;
    bool m_openRenamePopupRequested{false};
    bool m_openDeletePopupRequested{false};
    bool m_openImportPopupRequested{false};
    char m_renameBuffer[256] = "";
    char m_importSourceBuffer[1024] = "";
    char m_importDestinationBuffer[1024] = "";
    char m_importNameBuffer[256] = "";
    char m_importFilterBuffer[128] = "";
    int m_importTypeIndex{0};
    bool m_importRecursive{false};
    std::string m_importStatusMessage;
    bool m_importStatusIsError{false};
    std::filesystem::path m_importBrowserCurrentDirectory;
    std::unordered_set<std::string> m_importSelectedSourcePaths;

    std::unordered_set<std::string> m_textureExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tiff", ".psd", ".gif", ".hdr", ".exr", ".dds"};
    std::unordered_set<std::string> m_velixExtensions = {".scene", ".elixproject", ".cc", ".cxx"};
    std::unordered_set<std::string> m_cppExtensions = {".cpp", ".c", ".cc", ".cxx"};
    std::unordered_set<std::string> m_headerExtensions = {".h", ".hpp", ".hh", ".hxx"};
    std::unordered_set<std::string> m_modelExtensions = {".obj", ".fbx"};
    std::unordered_set<std::string> m_sceneExtensions = {".scene", ".json", ".yaml", ".yml"};
    std::unordered_set<std::string> m_shaderExtensions = {".glsl", ".vert", ".frag", ".geom", ".tesc", ".tese", ".comp", ".hlsl", ".fx"};
    std::unordered_set<std::string> m_configExtensions = {".ini", ".cfg", ".toml", ".xml", ".properties"};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_WINDOW_HPP