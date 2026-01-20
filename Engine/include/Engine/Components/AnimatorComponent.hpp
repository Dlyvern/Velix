#ifndef ELIX_ANIMATOR_COMPONENT_HPP
#define ELIX_ANIMATOR_COMPONENT_HPP

#include "Core/Macros.hpp"

#include "Engine/Components/ECS.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Engine/Skeleton.hpp"
#include <string>

class Entity;

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct SQT
{
    glm::quat rotation{1, 0, 0, 0};
    glm::vec3 position{0, 0, 0};
    glm::vec3 scale{0, 0, 0};
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
    double ticksPerSecond;
    double duration;
    std::vector<AnimationTrack> boneAnimations;
    Skeleton* skeletonForAnimation{nullptr};
    Entity* gameObject{nullptr};

    AnimationTrack* getAnimationTrack(const std::string& name)
    {
        const auto it = std::find_if(boneAnimations.begin(), boneAnimations.end(), [&name](const auto& bone) {return bone.objectName == name;});
        return it == boneAnimations.end() ? nullptr : &(*it);
    }
};

class AnimatorComponent final : public ECS
{
public:
    void update(float deltaTime) override;

    void playAnimation(Animation* animation, bool repeat = true);

    void stopAnimation();

    [[nodiscard]] bool isAnimationPlaying() const;
private:
    void calculateBoneTransform(Skeleton::BoneInfo* boneInfo, const glm::mat4 &parentTransform, Animation* animation, float currentTime);
    void calculateObjectTransform(Animation* animation, float currentTime);

    bool m_isAnimationPaused{false};
    bool m_isAnimationLooped{true};
    bool m_isAnimationCompleted{false};
    bool m_isInterpolating{false};

    float m_animationSpeed{1.0f};
    float m_currentTime{0.0f};
    float m_haltTime{0.0f};
    float m_interTime{0.0f};

    Animation* m_currentAnimation{nullptr};
    Animation* m_nextAnimation{nullptr};
    Animation* m_queueAnimation{nullptr};
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_ANIMATOR_COMPONENT_HPP