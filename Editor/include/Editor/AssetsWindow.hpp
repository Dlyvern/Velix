#ifndef ELIX_ASSETS_WINDOW_HPP
#define ELIX_ASSETS_WINDOW_HPP

#include "Core/Macros.hpp"

#include "Engine/Project.hpp"

#include "Editor/EditorResourcesStorage.hpp"

#include <string>
#include <filesystem>
#include <unordered_set>
#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class AssetsWindow
{
public:
    explicit AssetsWindow(EditorResourcesStorage *resourcesStorage);

    void setProject(engine::Project *project);

    void draw();

private:
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

    void navigateToDirectory(const std::filesystem::path &path);
    std::string formatFileSize(uintmax_t size) const;

    bool matchesSearch(const std::string &filename) const;
    void refreshCurrentDirectory();

    engine::Project *m_currentProject{nullptr};
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

    std::unordered_set<std::string> m_cppExtensions = {".cpp", ".c", ".cc", ".cxx"};
    std::unordered_set<std::string> m_headerExtensions = {".h", ".hpp", ".hh", ".hxx"};
    std::unordered_set<std::string> m_imageExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tiff", ".psd", ".gif"};
    std::unordered_set<std::string> m_modelExtensions = {".obj", ".fbx", ".gltf", ".glb", ".dae", ".blend", ".3ds"};
    std::unordered_set<std::string> m_sceneExtensions = {".scene", ".json", ".yaml", ".yml"};
    std::unordered_set<std::string> m_shaderExtensions = {".glsl", ".vert", ".frag", ".geom", ".tesc", ".tese", ".comp", ".hlsl", ".fx"};
    std::unordered_set<std::string> m_configExtensions = {".ini", ".cfg", ".toml", ".xml", ".properties"};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_WINDOW_HPP