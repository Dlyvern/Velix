#include "Engine/Components/AnimatorComponent.hpp"

#include "Engine/Entity.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    glm::vec3 interpolateVec3(const glm::vec3 &start, const glm::vec3 &end, float t)
    {
        return start + t * (end - start);
    }

    glm::quat interpolateQuat(const glm::quat &start, const glm::quat &end, float t)
    {
        return glm::slerp(start, end, t);
    }

    std::pair<const elix::engine::SQT *, const elix::engine::SQT *> findKeyframes(const std::vector<elix::engine::SQT> &keyFrames, const float currentTime)
    {
        if (keyFrames.empty())
            return {nullptr, nullptr};

        if (currentTime <= keyFrames.front().timeStamp)
            return {&keyFrames.front(), &keyFrames.front()};

        if (currentTime >= keyFrames.back().timeStamp)
            return {&keyFrames.back(), &keyFrames.back()};

        for (size_t i = 1; i < keyFrames.size(); ++i)
            if (currentTime < keyFrames[i].timeStamp)
                return {&keyFrames[i - 1], &keyFrames[i]};

        return {nullptr, nullptr};
    }
} // namespace

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void AnimatorComponent::onOwnerAttached()
{
    refreshAnimationBindings();
}

void AnimatorComponent::setAnimations(const std::vector<Animation> &animations, Skeleton *skeletonForAnimations)
{
    m_animations = animations;

    if (skeletonForAnimations)
        m_boundSkeleton = skeletonForAnimations;

    if (m_animations.empty())
        m_selectedAnimationIndex = -1;
    else if (m_selectedAnimationIndex < 0 || m_selectedAnimationIndex >= static_cast<int>(m_animations.size()))
        m_selectedAnimationIndex = 0;

    m_currentAnimation = nullptr;
    m_currentTime = 0.0f;
    m_isAnimationPaused = false;
    m_isAnimationCompleted = false;

    refreshAnimationBindings();
}

void AnimatorComponent::bindSkeleton(Skeleton *skeletonForAnimations)
{
    m_boundSkeleton = skeletonForAnimations;
    refreshAnimationBindings();
}

const std::vector<Animation> &AnimatorComponent::getAnimations() const
{
    return m_animations;
}

void AnimatorComponent::setSelectedAnimationIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_animations.size()))
    {
        m_selectedAnimationIndex = -1;
        return;
    }

    m_selectedAnimationIndex = index;
}

int AnimatorComponent::getSelectedAnimationIndex() const
{
    return m_selectedAnimationIndex;
}

bool AnimatorComponent::playAnimationByIndex(size_t index, bool repeat)
{
    if (index >= m_animations.size())
        return false;

    m_selectedAnimationIndex = static_cast<int>(index);
    playAnimation(&m_animations[index], repeat);
    return true;
}

bool AnimatorComponent::playAnimationByName(const std::string &name, bool repeat)
{
    for (size_t i = 0; i < m_animations.size(); ++i)
    {
        if (m_animations[i].name == name)
            return playAnimationByIndex(i, repeat);
    }

    return false;
}

void AnimatorComponent::update(float deltaTime)
{
    if (!m_currentAnimation || m_isAnimationPaused)
        return;

    const float duration = getCurrentAnimationDuration();
    const float ticksPerSecond = static_cast<float>(m_currentAnimation->ticksPerSecond > 0.0 ? m_currentAnimation->ticksPerSecond : 25.0);

    if (duration <= std::numeric_limits<float>::epsilon())
    {
        m_currentTime = 0.0f;
        applyCurrentAnimationPose();
        return;
    }

    m_currentTime += ticksPerSecond * deltaTime * m_animationSpeed;

    if (m_isAnimationLooped)
    {
        m_currentTime = std::fmod(m_currentTime, duration);
        if (m_currentTime < 0.0f)
            m_currentTime += duration;
    }
    else if (m_currentTime >= duration)
    {
        m_currentTime = duration;
        m_isAnimationCompleted = true;
        m_isAnimationPaused = true;
    }

    applyCurrentAnimationPose();
}

void AnimatorComponent::applyCurrentAnimationPose()
{
    if (!m_currentAnimation)
        return;

    if (m_currentAnimation->skeletonForAnimation)
    {
        auto *parent = m_currentAnimation->skeletonForAnimation->getParent();
        if (parent)
            calculateBoneTransform(parent, glm::mat4(1.0f), m_currentAnimation, m_currentTime);
    }
    else if (m_currentAnimation->gameObject)
        calculateObjectTransform(m_currentAnimation, m_currentTime);
}

void AnimatorComponent::calculateObjectTransform(Animation *animation, float currentTime)
{
    (void)animation;
    (void)currentTime;

    // TODO: Object animation tracks can be applied here once transform track extraction is added.
}

void AnimatorComponent::refreshAnimationBindings()
{
    auto *owner = getOwner<Entity>();

    for (auto &animation : m_animations)
    {
        animation.skeletonForAnimation = m_boundSkeleton;
        animation.gameObject = owner;
    }
}

void AnimatorComponent::playAnimation(Animation *animation, const bool repeat)
{
    if (!animation)
        return;

    m_isAnimationLooped = repeat;
    m_isAnimationPaused = false;
    m_isAnimationCompleted = false;
    m_currentAnimation = animation;
    m_currentTime = 0.0f;

    for (size_t i = 0; i < m_animations.size(); ++i)
    {
        if (&m_animations[i] == animation)
        {
            m_selectedAnimationIndex = static_cast<int>(i);
            break;
        }
    }

    applyCurrentAnimationPose();
}

void AnimatorComponent::stopAnimation()
{
    m_currentAnimation = nullptr;
    m_currentTime = 0.0f;
    m_isAnimationPaused = false;
    m_isAnimationCompleted = false;
}

void AnimatorComponent::setAnimationPaused(bool paused)
{
    m_isAnimationPaused = paused;
}

bool AnimatorComponent::isAnimationPaused() const
{
    return m_isAnimationPaused;
}

void AnimatorComponent::setAnimationLooped(bool looped)
{
    m_isAnimationLooped = looped;
}

bool AnimatorComponent::isAnimationLooped() const
{
    return m_isAnimationLooped;
}

void AnimatorComponent::setAnimationSpeed(float speed)
{
    m_animationSpeed = std::max(speed, 0.01f);
}

float AnimatorComponent::getAnimationSpeed() const
{
    return m_animationSpeed;
}

void AnimatorComponent::setCurrentTime(float currentTime)
{
    if (!m_currentAnimation)
    {
        m_currentTime = 0.0f;
        return;
    }

    const float duration = getCurrentAnimationDuration();
    if (duration <= std::numeric_limits<float>::epsilon())
    {
        m_currentTime = 0.0f;
        applyCurrentAnimationPose();
        return;
    }

    m_currentTime = std::clamp(currentTime, 0.0f, duration);
    applyCurrentAnimationPose();
}

float AnimatorComponent::getCurrentTime() const
{
    return m_currentTime;
}

float AnimatorComponent::getCurrentAnimationDuration() const
{
    if (!m_currentAnimation)
        return 0.0f;

    return static_cast<float>(std::max(0.0, m_currentAnimation->duration));
}

Animation *AnimatorComponent::getCurrentAnimation()
{
    return m_currentAnimation;
}

const Animation *AnimatorComponent::getCurrentAnimation() const
{
    return m_currentAnimation;
}

void AnimatorComponent::calculateBoneTransform(Skeleton::BoneInfo *boneInfo, const glm::mat4 &parentTransform, Animation *animation, const float currentTime)
{
    if (!boneInfo || !animation || !animation->skeletonForAnimation)
        return;

    const std::string &nodeName = boneInfo->name;
    glm::mat4 boneTransform = boneInfo->localBindTransform;

    if (const auto *boneAnimation = animation->getAnimationTrack(nodeName); boneAnimation && !boneAnimation->keyFrames.empty())
    {
        auto [startFrame, endFrame] = findKeyframes(boneAnimation->keyFrames, currentTime);

        if (!startFrame || !endFrame)
            return;

        float deltaTime = endFrame->timeStamp - startFrame->timeStamp;
        float t = (deltaTime == 0) ? 0.0f : (currentTime - startFrame->timeStamp) / deltaTime;
        t = glm::clamp(t, 0.0f, 1.0f);

        const glm::vec3 position = interpolateVec3(startFrame->position, endFrame->position, t);
        const glm::quat rotation = glm::normalize(interpolateQuat(startFrame->rotation, endFrame->rotation, t));
        const glm::vec3 scale = interpolateVec3(startFrame->scale, endFrame->scale, t);

        boneTransform = glm::translate(glm::mat4(1.0f), position) *
                        glm::toMat4(rotation) *
                        glm::scale(glm::mat4(1.0f), scale);
    }

    const glm::mat4 globalTransformation = parentTransform * boneTransform;
    boneInfo->finalTransformation = globalTransformation;

    auto *skeleton = animation->skeletonForAnimation;

    for (const auto &i : boneInfo->children)
    {
        const auto child = skeleton->getBone(i);
        if (child)
            calculateBoneTransform(child, globalTransformation, animation, currentTime);
    }
}

bool AnimatorComponent::isAnimationPlaying() const
{
    return m_currentAnimation != nullptr;
}

ELIX_NESTED_NAMESPACE_END
