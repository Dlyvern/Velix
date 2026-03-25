#ifndef ELIX_ENGINE_CONFIG_HPP
#define ELIX_ENGINE_CONFIG_HPP

#include "Core/Macros.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class EngineConfig
{
public:
    struct IdeInfo
    {
        std::string id;
        std::string displayName;
        std::string command;
    };

    static EngineConfig &instance();

    bool reload();
    bool save() const;

    const std::filesystem::path &getConfigDirectory() const;
    const std::filesystem::path &getConfigFilePath() const;

    const std::vector<IdeInfo> &getDetectedIdes() const;
    std::optional<IdeInfo> findIde(const std::string &ideId) const;

    const std::string &getPreferredIdeId() const;
    void setPreferredIdeId(const std::string &ideId);

    bool getShowAssetThumbnails() const;
    void setShowAssetThumbnails(bool enabled);

    bool getShowModelAssetPreviews() const;
    void setShowModelAssetPreviews(bool enabled);

    bool getShowMaterialAssetPreviews() const;
    void setShowMaterialAssetPreviews(bool enabled);

    bool getShowTextureAssetPreviews() const;
    void setShowTextureAssetPreviews(bool enabled);

    bool getShowEditorBillboards() const;
    void setShowEditorBillboards(bool enabled);

    bool getShowHierarchyPanel() const;
    void setShowHierarchyPanel(bool enabled);

    bool getShowDetailsPanel() const;
    void setShowDetailsPanel(bool enabled);

    float getRightSidebarSplitRatio() const;
    void setRightSidebarSplitRatio(float ratio);

    bool getDetailedRenderProfilingEnabled() const;
    void setDetailedRenderProfilingEnabled(bool enabled);

    bool getSceneAutosaveEnabled() const;
    void setSceneAutosaveEnabled(bool enabled);

    float getSceneAutosaveIntervalMinutes() const;
    void setSceneAutosaveIntervalMinutes(float minutes);

    bool getSSREnabled() const;
    void setSSREnabled(bool enabled);

    float getSSRMaxDistance() const;
    void setSSRMaxDistance(float distance);

    float getSSRThickness() const;
    void setSSRThickness(float thickness);

    float getSSRStrength() const;
    void setSSRStrength(float strength);

    int getSSRSteps() const;
    void setSSRSteps(int steps);

    float getSSRRoughnessCutoff() const;
    void setSSRRoughnessCutoff(float cutoff);

    std::optional<IdeInfo> findPreferredVSCodeIde() const;
    bool hasVSCodeIde() const;

private:
    EngineConfig() = default;

    static std::filesystem::path resolveConfigDirectory();
    void detectInstalledIdes();
    void applyDefaults();
    bool loadFromDisk();
    bool isKnownIdeId(const std::string &ideId) const;

private:
    std::filesystem::path m_configDirectory;
    std::filesystem::path m_configFilePath;
    std::string m_preferredIdeId{"vscode"};
    bool m_showAssetThumbnails{true};
    bool m_showModelAssetPreviews{true};
    bool m_showMaterialAssetPreviews{true};
    bool m_showTextureAssetPreviews{true};
    bool m_showEditorBillboards{true};
    bool m_showHierarchyPanel{true};
    bool m_showDetailsPanel{true};
    float m_rightSidebarSplitRatio{0.5f};
    bool m_detailedRenderProfilingEnabled{true};
    bool m_sceneAutosaveEnabled{true};
    float m_sceneAutosaveIntervalMinutes{5.0f};
    bool m_enableSSR{false};
    float m_ssrMaxDistance{15.0f};
    float m_ssrThickness{0.03f};
    float m_ssrStrength{1.0f};
    int m_ssrSteps{48};
    float m_ssrRoughnessCutoff{0.4f};
    std::vector<IdeInfo> m_detectedIdes;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ENGINE_CONFIG_HPP
