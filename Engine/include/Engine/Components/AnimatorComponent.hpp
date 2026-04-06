#ifndef ELIX_ANIMATOR_COMPONENT_HPP
#define ELIX_ANIMATOR_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"
#include "Engine/Animation/AnimationTree.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Engine/Skeleton.hpp"
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Entity;

struct SQT
{
    glm::quat rotation{1, 0, 0, 0};
    glm::vec3 position{0, 0, 0};
    glm::vec3 scale{1, 1, 1};
    float timeStamp{0.0f};
};

struct AnimationTrack
{
    std::vector<SQT> keyFrames;
    std::string objectName;
};

struct Animation
{
    std::string name;
    double ticksPerSecond{0.0};
    double duration{0.0};
    std::vector<AnimationTrack> boneAnimations;
    Skeleton *skeletonForAnimation{nullptr};
    Entity *gameObject{nullptr};

    AnimationTrack *getAnimationTrack(const std::string &name)
    {
        const auto it = std::find_if(boneAnimations.begin(), boneAnimations.end(), [&name](const auto &bone)
                                     { return bone.objectName == name; });
        return it == boneAnimations.end() ? nullptr : &(*it);
    }

    const AnimationTrack *getAnimationTrack(const std::string &name) const
    {
        const auto it = std::find_if(boneAnimations.begin(), boneAnimations.end(), [&name](const auto &bone)
                                     { return bone.objectName == name; });
        return it == boneAnimations.end() ? nullptr : &(*it);
    }
};

class AnimatorComponent final : public ECS
{
public:
    void update(float deltaTime) override;
    void onOwnerAttached() override;

    void setAnimations(const std::vector<Animation> &animations, Skeleton *skeletonForAnimations = nullptr);
    void bindSkeleton(Skeleton *skeletonForAnimations);

    const std::vector<Animation> &getAnimations() const;
    void setExternalAnimationAssetPaths(const std::vector<std::string> &assetPaths);
    void addExternalAnimationAssetPath(const std::string &assetPath);
    const std::vector<std::string> &getExternalAnimationAssetPaths() const;

    void setSelectedAnimationIndex(int index);
    [[nodiscard]] int getSelectedAnimationIndex() const;

    bool playAnimationByIndex(size_t index, bool repeat = true);
    bool playAnimationByName(const std::string &name, bool repeat = true);

    void playAnimation(Animation *animation, bool repeat = true);

    void stopAnimation();

    void setAnimationPaused(bool paused);
    [[nodiscard]] bool isAnimationPaused() const;

    void setAnimationLooped(bool looped);
    [[nodiscard]] bool isAnimationLooped() const;

    void setAnimationSpeed(float speed);
    [[nodiscard]] float getAnimationSpeed() const;

    void setCurrentTime(float currentTime);
    [[nodiscard]] float getCurrentTime() const;
    [[nodiscard]] float getCurrentAnimationDuration() const;

    Animation *getCurrentAnimation();
    const Animation *getCurrentAnimation() const;

    [[nodiscard]] bool isAnimationPlaying() const;

    void loadTree(const std::string &assetPath);
    void setTree(const AnimationTree &tree);
    void clearTree();
    [[nodiscard]] bool hasTree() const;
    [[nodiscard]] const AnimationTree *getTree() const;

    void setFloat(const std::string &name, float value);
    void setBool(const std::string &name, bool value);
    void setInt(const std::string &name, int value);
    void setTrigger(const std::string &name);
    [[nodiscard]] float getFloat(const std::string &name) const;
    [[nodiscard]] bool getBool(const std::string &name) const;
    [[nodiscard]] int getInt(const std::string &name) const;

    [[nodiscard]] std::string getCurrentStateName() const;
    [[nodiscard]] std::string getCurrentStatePath() const;
    [[nodiscard]] std::string getActiveMachinePath() const;
    [[nodiscard]] float getCurrentStateNormalizedTime() const;
    [[nodiscard]] bool isInTransition() const;

    // Post-animation hooks — called after bone matrices are fully written each frame.
    // IK solvers and other post-process components register here via onOwnerAttached().
    using PostAnimHook = std::function<void(Skeleton &)>;
    void addPostAnimHook(const void *ownerKey, PostAnimHook fn);
    void removePostAnimHook(const void *ownerKey);

private:
    struct TreePoseSource
    {
        const Animation *primary{nullptr};
        const Animation *secondary{nullptr};
        float primaryTicks{0.0f};
        float secondaryTicks{0.0f};
        float sampleBlend{0.0f};
    };

    void applyCurrentAnimationPose();
    void refreshAnimationBindings();

    void calculateBoneTransform(Skeleton::BoneInfo *boneInfo, const glm::mat4 &parentTransform, Animation *animation, float currentTime);
    void calculateObjectTransform(Animation *animation, float currentTime);

    void resetTreeRuntime();
    void cacheTreeAnimations();
    void initTreeParams();
    void ensureTreeActivePath();
    void evaluateTransitions();
    bool checkConditions(const std::vector<AnimationTransitionCondition> &conditions,
                         const AnimationTreeNode *leafNode,
                         float leafElapsedSeconds) const;
    void startTransition(const std::vector<int> &targetPath, float blendDuration);
    void applyTreePose();
    void applyBlendedBoneTransform(Skeleton::BoneInfo *bone, const glm::mat4 &parentTransform,
                                   const TreePoseSource &poseA,
                                   const TreePoseSource *poseB,
                                   float blend);
    [[nodiscard]] float secondsToTicks(const Animation *anim, float seconds) const;
    [[nodiscard]] const Animation *getAnimationClip(const std::string &assetPath, int clipIndex) const;
    [[nodiscard]] const AnimationTreeNode *getCurrentLeafNode() const;
    [[nodiscard]] const AnimationTreeNode *getNextLeafNode() const;
    [[nodiscard]] bool resolveMachineEntryPath(int machineNodeId, std::vector<int> &outPath) const;
    [[nodiscard]] bool resolveNodePath(int nodeId, std::vector<int> &outPath) const;
    [[nodiscard]] std::vector<int> buildTargetPath(int machineNodeId, int targetNodeId) const;
    [[nodiscard]] float getNodeDurationSeconds(const AnimationTreeNode *node) const;
    [[nodiscard]] float getNodeNormalizedTime(const AnimationTreeNode *node, float elapsedSeconds) const;
    [[nodiscard]] bool isNodeFinished(const AnimationTreeNode *node, float elapsedSeconds) const;
    void buildPoseSource(const AnimationTreeNode *node, float elapsedSeconds, TreePoseSource &outPose) const;

    bool m_isAnimationPaused{false};
    bool m_isAnimationLooped{true};
    bool m_isAnimationCompleted{false};

    float m_animationSpeed{1.0f};
    float m_currentTime{0.0f};

    Skeleton *m_boundSkeleton{nullptr};
    std::vector<Animation> m_animations;
    std::vector<std::string> m_externalAnimationAssetPaths;
    int m_selectedAnimationIndex{-1};
    Animation *m_currentAnimation{nullptr};

    // Tree mode state
    std::optional<AnimationTree> m_tree;
    std::unordered_map<std::string, float> m_floats;
    std::unordered_map<std::string, bool> m_bools;
    std::unordered_map<std::string, int> m_ints;
    std::unordered_set<std::string> m_triggers;

    int m_currentStateNodeId{-1};
    int m_nextStateNodeId{-1};
    std::vector<int> m_currentStatePath;
    std::vector<int> m_nextStatePath;
    float m_currentStateTimeSec{0.0f};
    float m_nextStateTimeSec{0.0f};
    float m_blendAlpha{0.0f};
    float m_blendDuration{0.3f};
    float m_transitionElapsed{0.0f};

    // Cached clips referenced by the animation tree.
    std::unordered_map<std::string, std::vector<Animation>> m_treeClipAssets;

    struct PostAnimHookEntry { const void *key; PostAnimHook fn; };
    std::vector<PostAnimHookEntry> m_postAnimHooks;

    void firePostAnimHooks();
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATOR_COMPONENT_HPP
