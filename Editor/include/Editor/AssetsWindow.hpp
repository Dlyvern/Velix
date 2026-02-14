#ifndef ELIX_ASSETS_WINDOW_HPP
#define ELIX_ASSETS_WINDOW_HPP

#include "Core/Macros.hpp"

#include "Editor/Project.hpp"

#include "Editor/EditorResourcesStorage.hpp"

#include "Engine/Material.hpp"

#include <string>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <functional>

ELIX_NESTED_NAMESPACE_BEGIN(editor)

class AssetsWindow
{
public:
    AssetsWindow(EditorResourcesStorage *resourcesStorage, VkDescriptorPool descriptorPool, std::vector<engine::Material *> &previewMaterialJobs);

    void setProject(Project *project);

    void draw();

    void setDoneMaterialJobs(const std::vector<VkDescriptorSet> &doneMaterialPreviewJobs)
    {
        m_doneMaterialPreviewJobs = doneMaterialPreviewJobs;
    }

private:
    std::pair<engine::Texture::SharedPtr, elix::engine::CPUMaterial> tryToPreloadTexture(const std::string &path);

    std::vector<VkDescriptorSet> m_doneMaterialPreviewJobs;
    std::unordered_map<std::string, uint32_t> m_materialPreviewSlots;

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

    void drawMaterialEditor();

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

    std::unordered_set<std::string> m_velixExtensions = {".scene", ".elixproject", ".cc", ".cxx"};

    std::unordered_set<std::string> m_cppExtensions = {".cpp", ".c", ".cc", ".cxx"};
    std::unordered_set<std::string> m_headerExtensions = {".h", ".hpp", ".hh", ".hxx"};
    std::unordered_set<std::string> m_imageExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tiff", ".psd", ".gif"};
    std::unordered_set<std::string> m_modelExtensions = {".obj", ".fbx", ".gltf", ".glb", ".dae", ".blend", ".3ds"};
    std::unordered_set<std::string> m_sceneExtensions = {".scene", ".json", ".yaml", ".yml"};
    std::unordered_set<std::string> m_shaderExtensions = {".glsl", ".vert", ".frag", ".geom", ".tesc", ".tese", ".comp", ".hlsl", ".fx"};
    std::unordered_set<std::string> m_configExtensions = {".ini", ".cfg", ".toml", ".xml", ".properties"};

    // TODO remove this shit from here.....

    struct MaterialAssetWindow
    {
        engine::Material::SharedPtr material{nullptr};
        engine::Texture::SharedPtr texture{nullptr};
        VkDescriptorSet previewTextureDescriptorSet{nullptr};
    };

    struct TextureAssetWindow
    {
        engine::Texture::SharedPtr texture{nullptr};
        VkDescriptorSet previewTextureDescriptorSet{nullptr};
    };

    std::unordered_map<std::string, TextureAssetWindow> m_texturesPreview;

    std::vector<engine::Material *> &m_previewMaterialJobs;

    std::unordered_map<std::string, MaterialAssetWindow> m_materials;
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    std::string m_currentEditedMaterialPath;

    bool m_shotTexturePopup{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSETS_WINDOW_HPP