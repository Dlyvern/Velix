#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Entity.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

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

    void uniquifyAnimationNames(std::vector<elix::engine::Animation> &animations)
    {
        std::unordered_set<std::string> usedNames;
        usedNames.reserve(animations.size());

        for (auto &animation : animations)
        {
            std::string baseName = animation.name.empty() ? std::string("Animation") : animation.name;
            std::string candidate = baseName;
            std::size_t suffix = 2u;

            while (usedNames.contains(candidate))
            {
                candidate = baseName + " (" + std::to_string(suffix) + ")";
                ++suffix;
            }

            animation.name = std::move(candidate);
            usedNames.insert(animation.name);
        }
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
    uniquifyAnimationNames(m_animations);

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

void AnimatorComponent::setExternalAnimationAssetPaths(const std::vector<std::string> &assetPaths)
{
    m_externalAnimationAssetPaths.clear();
    m_externalAnimationAssetPaths.reserve(assetPaths.size());

    for (const auto &assetPath : assetPaths)
        addExternalAnimationAssetPath(assetPath);
}

void AnimatorComponent::addExternalAnimationAssetPath(const std::string &assetPath)
{
    if (assetPath.empty())
        return;

    if (std::find(m_externalAnimationAssetPaths.begin(), m_externalAnimationAssetPaths.end(), assetPath) != m_externalAnimationAssetPaths.end())
        return;

    m_externalAnimationAssetPaths.push_back(assetPath);
}

const std::vector<std::string> &AnimatorComponent::getExternalAnimationAssetPaths() const
{
    return m_externalAnimationAssetPaths;
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
    if (m_tree.has_value())
    {
        if (m_currentStateIndex == -1 && !m_tree->states.empty())
        {
            m_currentStateIndex = m_tree->entryStateIndex;
            m_currentStateTimeSec = 0.0f;
            initTreeParams();
        }

        if (m_currentStateIndex < 0 || m_currentStateIndex >= static_cast<int>(m_tree->states.size()))
            return;

        const auto &curState = m_tree->states[static_cast<size_t>(m_currentStateIndex)];
        m_currentStateTimeSec += deltaTime * curState.speed;

        if (m_nextStateIndex >= 0)
        {
            m_transitionElapsed += deltaTime;
            m_blendAlpha = (m_blendDuration > 0.0f) ? std::min(m_transitionElapsed / m_blendDuration, 1.0f) : 1.0f;
            m_nextStateTimeSec += deltaTime;

            if (m_blendAlpha >= 1.0f)
            {
                m_currentStateIndex = m_nextStateIndex;
                m_currentStateTimeSec = m_nextStateTimeSec;
                m_nextStateIndex = -1;
                m_blendAlpha = 0.0f;
                m_transitionElapsed = 0.0f;
                m_nextStateTimeSec = 0.0f;
            }
        }

        evaluateTransitions();
        m_triggers.clear();
        applyTreePose();
        return;
    }

    // Simple mode
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
        auto *skeleton = m_currentAnimation->skeletonForAnimation;
        const glm::mat4 identity(1.0f);

        for (size_t boneIndex = 0; boneIndex < skeleton->getBonesCount(); ++boneIndex)
        {
            auto *rootBone = skeleton->getBone(static_cast<int>(boneIndex));
            if (!rootBone || rootBone->parentId != -1)
                continue;

            calculateBoneTransform(rootBone, identity, m_currentAnimation, m_currentTime);
        }
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

void AnimatorComponent::loadTree(const std::string &assetPath)
{
    auto loaded = engine::AssetsLoader::loadAnimationTree(assetPath);
    if (!loaded.has_value())
        return;
    setTree(loaded.value());
}

void AnimatorComponent::setTree(const AnimationTree &tree)
{
    m_tree = tree;
    m_currentStateIndex = -1;
    m_nextStateIndex = -1;
    m_blendAlpha = 0.0f;
    m_transitionElapsed = 0.0f;
    m_currentStateTimeSec = 0.0f;
    m_nextStateTimeSec = 0.0f;

    const size_t stateCount = m_tree->states.size();
    m_treeStateClips.resize(stateCount);
    m_stateAnims.assign(stateCount, nullptr);

    for (size_t i = 0; i < stateCount; ++i)
    {
        const auto &state = m_tree->states[i];
        if (state.animationAssetPath.empty())
            continue;

        auto animAsset = engine::AssetsLoader::loadAnimationAsset(state.animationAssetPath);
        if (!animAsset.has_value() || animAsset->animations.empty())
            continue;

        m_treeStateClips[i] = std::move(animAsset->animations);

        const int clipIdx = std::clamp(state.clipIndex, 0, static_cast<int>(m_treeStateClips[i].size()) - 1);
        m_stateAnims[i] = &m_treeStateClips[i][static_cast<size_t>(clipIdx)];

        if (m_boundSkeleton)
            m_treeStateClips[i][static_cast<size_t>(clipIdx)].skeletonForAnimation = m_boundSkeleton;
    }

    initTreeParams();
}

void AnimatorComponent::clearTree()
{
    m_tree.reset();
    m_treeStateClips.clear();
    m_stateAnims.clear();
    m_floats.clear();
    m_bools.clear();
    m_ints.clear();
    m_triggers.clear();
    m_currentStateIndex = -1;
    m_nextStateIndex = -1;
}

bool AnimatorComponent::hasTree() const { return m_tree.has_value(); }

const AnimationTree *AnimatorComponent::getTree() const
{
    return m_tree.has_value() ? &m_tree.value() : nullptr;
}

void AnimatorComponent::setFloat(const std::string &name, float value) { m_floats[name] = value; }
void AnimatorComponent::setBool(const std::string &name, bool value) { m_bools[name] = value; }
void AnimatorComponent::setInt(const std::string &name, int value) { m_ints[name] = value; }
void AnimatorComponent::setTrigger(const std::string &name) { m_triggers.insert(name); }

float AnimatorComponent::getFloat(const std::string &name) const
{
    const auto it = m_floats.find(name);
    return it != m_floats.end() ? it->second : 0.0f;
}

bool AnimatorComponent::getBool(const std::string &name) const
{
    const auto it = m_bools.find(name);
    return it != m_bools.end() ? it->second : false;
}

int AnimatorComponent::getInt(const std::string &name) const
{
    const auto it = m_ints.find(name);
    return it != m_ints.end() ? it->second : 0;
}

std::string AnimatorComponent::getCurrentStateName() const
{
    if (!m_tree.has_value() || m_currentStateIndex < 0 ||
        m_currentStateIndex >= static_cast<int>(m_tree->states.size()))
        return {};
    return m_tree->states[static_cast<size_t>(m_currentStateIndex)].name;
}

float AnimatorComponent::getCurrentStateNormalizedTime() const
{
    if (!m_tree.has_value() || m_currentStateIndex < 0)
        return 0.0f;
    const Animation *anim = getStateAnimation(m_currentStateIndex);
    if (!anim || anim->ticksPerSecond <= 0.0 || anim->duration <= 0.0)
        return 0.0f;
    const float durationSec = static_cast<float>(anim->duration / anim->ticksPerSecond);
    return durationSec > 0.0f ? std::fmod(m_currentStateTimeSec, durationSec) / durationSec : 0.0f;
}

bool AnimatorComponent::isInTransition() const { return m_nextStateIndex >= 0; }

void AnimatorComponent::initTreeParams()
{
    if (!m_tree.has_value())
        return;

    m_floats.clear();
    m_bools.clear();
    m_ints.clear();
    m_triggers.clear();

    for (const auto &param : m_tree->parameters)
    {
        switch (param.type)
        {
        case AnimationTreeParameter::Type::Float:
            m_floats[param.name] = param.floatDefault;
            break;
        case AnimationTreeParameter::Type::Bool:
            m_bools[param.name] = param.boolDefault;
            break;
        case AnimationTreeParameter::Type::Int:
            m_ints[param.name] = param.intDefault;
            break;
        case AnimationTreeParameter::Type::Trigger:
            break;
        }
    }
}

bool AnimatorComponent::checkConditions(const AnimationTransition &t) const
{
    for (const auto &cond : t.conditions)
    {
        switch (cond.type)
        {
        case AnimationTransitionCondition::Type::FloatGreater:
        {
            const auto it = m_floats.find(cond.parameterName);
            if (it == m_floats.end() || !(it->second > cond.floatThreshold))
                return false;
            break;
        }
        case AnimationTransitionCondition::Type::FloatLess:
        {
            const auto it = m_floats.find(cond.parameterName);
            if (it == m_floats.end() || !(it->second < cond.floatThreshold))
                return false;
            break;
        }
        case AnimationTransitionCondition::Type::BoolTrue:
        {
            const auto it = m_bools.find(cond.parameterName);
            if (it == m_bools.end() || !it->second)
                return false;
            break;
        }
        case AnimationTransitionCondition::Type::BoolFalse:
        {
            const auto it = m_bools.find(cond.parameterName);
            if (it == m_bools.end() || it->second)
                return false;
            break;
        }
        case AnimationTransitionCondition::Type::IntEqual:
        {
            const auto it = m_ints.find(cond.parameterName);
            if (it == m_ints.end() || it->second != cond.intValue)
                return false;
            break;
        }
        case AnimationTransitionCondition::Type::Trigger:
        {
            if (m_triggers.find(cond.parameterName) == m_triggers.end())
                return false;
            break;
        }
        }
    }
    return true;
}

void AnimatorComponent::startTransition(int targetIndex, float blendDuration)
{
    if (m_nextStateIndex >= 0)
        return;
    m_nextStateIndex = targetIndex;
    m_blendDuration = blendDuration;
    m_blendAlpha = 0.0f;
    m_transitionElapsed = 0.0f;
    m_nextStateTimeSec = 0.0f;
}

void AnimatorComponent::evaluateTransitions()
{
    if (!m_tree.has_value() || m_nextStateIndex >= 0)
        return;

    for (const auto &transition : m_tree->transitions)
    {
        const bool fromCurrent = (transition.fromStateIndex == m_currentStateIndex);
        const bool fromAny = (transition.fromStateIndex == -1);
        if (!fromCurrent && !fromAny)
            continue;
        if (transition.toStateIndex == m_currentStateIndex)
            continue;
        if (transition.hasExitTime && getCurrentStateNormalizedTime() < transition.exitTime)
            continue;
        if (!checkConditions(transition))
            continue;
        startTransition(transition.toStateIndex, transition.blendDuration);
        break;
    }
}

float AnimatorComponent::secondsToTicks(const Animation *anim, float seconds) const
{
    if (!anim || anim->ticksPerSecond <= 0.0)
        return 0.0f;
    return seconds * static_cast<float>(anim->ticksPerSecond);
}

const Animation *AnimatorComponent::getStateAnimation(int stateIndex) const
{
    if (stateIndex < 0 || stateIndex >= static_cast<int>(m_stateAnims.size()))
        return nullptr;
    return m_stateAnims[static_cast<size_t>(stateIndex)];
}

void AnimatorComponent::applyBlendedBoneTransform(Skeleton::BoneInfo *bone, const glm::mat4 &parentTransform,
                                                  const Animation *animA, const float ticksA,
                                                  const Animation *animB, const float ticksB,
                                                  const float blend)
{
    if (!bone)
        return;

    glm::vec3 posA(bone->localBindTransform[3]);
    glm::quat rotA = glm::quat_cast(glm::mat3(bone->localBindTransform));
    glm::vec3 scaA(1.0f);

    if (animA)
    {
        if (const auto *track = animA->getAnimationTrack(bone->name); track && !track->keyFrames.empty())
        {
            auto [s, e] = findKeyframes(track->keyFrames, ticksA);
            if (s && e)
            {
                float dt = e->timeStamp - s->timeStamp;
                float t = (dt == 0.0f) ? 0.0f : glm::clamp((ticksA - s->timeStamp) / dt, 0.0f, 1.0f);
                posA = interpolateVec3(s->position, e->position, t);
                rotA = glm::normalize(interpolateQuat(s->rotation, e->rotation, t));
                scaA = interpolateVec3(s->scale, e->scale, t);
            }
        }
    }

    glm::vec3 posB = posA, scaB = scaA;
    glm::quat rotB = rotA;

    if (animB)
    {
        if (const auto *track = animB->getAnimationTrack(bone->name); track && !track->keyFrames.empty())
        {
            auto [s, e] = findKeyframes(track->keyFrames, ticksB);
            if (s && e)
            {
                float dt = e->timeStamp - s->timeStamp;
                float t = (dt == 0.0f) ? 0.0f : glm::clamp((ticksB - s->timeStamp) / dt, 0.0f, 1.0f);
                posB = interpolateVec3(s->position, e->position, t);
                rotB = glm::normalize(interpolateQuat(s->rotation, e->rotation, t));
                scaB = interpolateVec3(s->scale, e->scale, t);
            }
        }
    }

    const glm::vec3 pos = interpolateVec3(posA, posB, blend);
    const glm::quat rot = glm::normalize(interpolateQuat(rotA, rotB, blend));
    const glm::vec3 sca = interpolateVec3(scaA, scaB, blend);

    const glm::mat4 local = glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(rot) * glm::scale(glm::mat4(1.0f), sca);
    const glm::mat4 global = parentTransform * local;
    bone->finalTransformation = global;

    Skeleton *skel = m_boundSkeleton
                         ? m_boundSkeleton
                         : (animA ? animA->skeletonForAnimation : nullptr);
    if (!skel)
        skel = animB ? animB->skeletonForAnimation : nullptr;
    if (!skel)
        return;

    for (const int childId : bone->children)
    {
        if (auto *child = skel->getBone(childId))
            applyBlendedBoneTransform(child, global, animA, ticksA, animB, ticksB, blend);
    }
}

void AnimatorComponent::applyTreePose()
{
    const Animation *animA = getStateAnimation(m_currentStateIndex);
    const Animation *animB = (m_nextStateIndex >= 0) ? getStateAnimation(m_nextStateIndex) : nullptr;

    if (m_boundSkeleton)
    {
        if (animA && !animA->skeletonForAnimation)
            const_cast<Animation *>(animA)->skeletonForAnimation = m_boundSkeleton;
        if (animB && !animB->skeletonForAnimation)
            const_cast<Animation *>(animB)->skeletonForAnimation = m_boundSkeleton;
    }

    Skeleton *skel = m_boundSkeleton
                         ? m_boundSkeleton
                         : (animA ? animA->skeletonForAnimation : nullptr);
    if (!skel && animB)
        skel = animB->skeletonForAnimation;
    if (!skel)
        return;

    auto wrapTicks = [](float ticks, const Animation *anim) -> float
    {
        if (!anim || anim->duration <= 0.0)
            return ticks;
        return std::fmod(ticks, static_cast<float>(anim->duration));
    };

    const float wrappedA = wrapTicks(secondsToTicks(animA, m_currentStateTimeSec), animA);
    const float wrappedB = wrapTicks(secondsToTicks(animB, m_nextStateTimeSec), animB);

    const glm::mat4 identity(1.0f);
    for (size_t i = 0; i < skel->getBonesCount(); ++i)
    {
        auto *root = skel->getBone(static_cast<int>(i));
        if (!root || root->parentId != -1)
            continue;
        applyBlendedBoneTransform(root, identity, animA, wrappedA, animB, wrappedB, m_blendAlpha);
    }
}

ELIX_NESTED_NAMESPACE_END