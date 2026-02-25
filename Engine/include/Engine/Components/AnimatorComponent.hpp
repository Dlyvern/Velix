#ifndef ELIX_ANIMATOR_COMPONENT_HPP
#define ELIX_ANIMATOR_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Engine/Skeleton.hpp"
#include <algorithm>
#include <string>
#include <cstddef>

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

private:
    void applyCurrentAnimationPose();
    void refreshAnimationBindings();

    void calculateBoneTransform(Skeleton::BoneInfo *boneInfo, const glm::mat4 &parentTransform, Animation *animation, float currentTime);
    void calculateObjectTransform(Animation *animation, float currentTime);

    bool m_isAnimationPaused{false};
    bool m_isAnimationLooped{true};
    bool m_isAnimationCompleted{false};

    float m_animationSpeed{1.0f};
    float m_currentTime{0.0f};

    Skeleton *m_boundSkeleton{nullptr};
    std::vector<Animation> m_animations;
    int m_selectedAnimationIndex{-1};
    Animation *m_currentAnimation{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ANIMATOR_COMPONENT_HPP
