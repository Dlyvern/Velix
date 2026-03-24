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
    [[nodiscard]] float getCurrentStateNormalizedTime() const;
    [[nodiscard]] bool isInTransition() const;

private:
    void applyCurrentAnimationPose();
    void refreshAnimationBindings();

    void calculateBoneTransform(Skeleton::BoneInfo *boneInfo, const glm::mat4 &parentTransform, Animation *animation, float currentTime);
    void calculateObjectTransform(Animation *animation, float currentTime);

    void initTreeParams();
    void evaluateTransitions();
    bool checkConditions(const AnimationTransition &t) const;
    void startTransition(int targetIndex, float blendDuration);
    void applyTreePose();
    void applyBlendedBoneTransform(Skeleton::BoneInfo *bone, const glm::mat4 &parentTransform,
                                   const Animation *animA, float ticksA,
                                   const Animation *animB, float ticksB,
                                   float blend);
    [[nodiscard]] float secondsToTicks(const Animation *anim, float seconds) const;
    [[nodiscard]] const Animation *getStateAnimation(int stateIndex) const;

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

    int m_currentStateIndex{-1};
    int m_nextStateIndex{-1};
    float m_currentStateTimeSec{0.0f};
    float m_nextStateTimeSec{0.0f};
    float m_blendAlpha{0.0f};
    float m_blendDuration{0.3f};
    float m_transitionElapsed{0.0f};

    // Cached clips per state (owned by AnimatorComponent, indexed by state)
    std::vector<std::vector<Animation>> m_treeStateClips;
    std::vector<const Animation *> m_stateAnims;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATOR_COMPONENT_HPP
