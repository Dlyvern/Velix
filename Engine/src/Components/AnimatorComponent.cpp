#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Entity.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace
{
    constexpr float kFloatConditionEqualityEpsilon = 0.001f;

    float getAnimationDurationSeconds(const elix::engine::Animation *anim)
    {
        if (!anim || anim->ticksPerSecond <= 0.0 || anim->duration <= 0.0)
            return 0.0f;

        return static_cast<float>(anim->duration / anim->ticksPerSecond);
    }

    float getStateNormalizedTime(float elapsedSeconds, const elix::engine::Animation *anim, bool loop)
    {
        const float durationSec = getAnimationDurationSeconds(anim);
        if (durationSec <= 0.0f)
            return 0.0f;

        if (loop)
        {
            const float wrapped = std::fmod(elapsedSeconds, durationSec);
            return (wrapped >= 0.0f ? wrapped : wrapped + durationSec) / durationSec;
        }

        return glm::clamp(elapsedSeconds / durationSec, 0.0f, 1.0f);
    }

    bool isStateFinished(float elapsedSeconds, const elix::engine::Animation *anim, bool loop)
    {
        if (loop)
            return false;

        const float durationSec = getAnimationDurationSeconds(anim);
        return durationSec > 0.0f && elapsedSeconds >= durationSec;
    }

    float resolveStatePlaybackTicks(float elapsedSeconds, const elix::engine::Animation *anim, bool loop)
    {
        if (!anim || anim->duration <= 0.0)
            return elapsedSeconds;

        const float ticks = elapsedSeconds * static_cast<float>(anim->ticksPerSecond);
        const float durationTicks = static_cast<float>(anim->duration);
        if (loop)
        {
            const float wrapped = std::fmod(ticks, durationTicks);
            return wrapped >= 0.0f ? wrapped : wrapped + durationTicks;
        }

        return glm::clamp(ticks, 0.0f, durationTicks);
    }

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
        ensureTreeActivePath();

        const AnimationTreeNode *currentLeaf = getCurrentLeafNode();
        if (!currentLeaf)
            return;

        m_currentStateTimeSec += deltaTime * std::max(currentLeaf->speed, 0.0f);

        if (m_nextStateNodeId >= 0)
        {
            if (const AnimationTreeNode *nextLeaf = getNextLeafNode())
                m_nextStateTimeSec += deltaTime * std::max(nextLeaf->speed, 0.0f);

            m_transitionElapsed += deltaTime;
            m_blendAlpha = (m_blendDuration > 0.0f) ? std::min(m_transitionElapsed / m_blendDuration, 1.0f) : 1.0f;

            if (m_blendAlpha >= 1.0f)
            {
                m_currentStatePath = m_nextStatePath;
                m_currentStateNodeId = m_nextStateNodeId;
                m_currentStateTimeSec = m_nextStateTimeSec;
                m_nextStatePath.clear();
                m_nextStateNodeId = -1;
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

    // Also bind tree clips so tree-mode animations work when the skeleton
    // arrives after loadTree() (async streaming).
    for (auto &[assetPath, clips] : m_treeClipAssets)
    {
        (void)assetPath;
        for (auto &clip : clips)
        {
            clip.skeletonForAnimation = m_boundSkeleton;
            clip.gameObject = owner;
        }
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
    if (m_tree.has_value())
    {
        return getCurrentLeafNode() != nullptr || getNextLeafNode() != nullptr;
    }

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
    AnimationTree runtimeTree = tree;
    runtimeTree.ensureGraph();
    m_tree = std::move(runtimeTree);
    m_currentAnimation = nullptr;
    m_currentTime = 0.0f;
    resetTreeRuntime();
    cacheTreeAnimations();
    initTreeParams();
    ensureTreeActivePath();
}

void AnimatorComponent::clearTree()
{
    m_tree.reset();
    m_treeClipAssets.clear();
    m_floats.clear();
    m_bools.clear();
    m_ints.clear();
    m_triggers.clear();
    resetTreeRuntime();
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
    const AnimationTreeNode *node = getCurrentLeafNode();
    if (!node)
        return {};
    return node->name;
}

std::string AnimatorComponent::getCurrentStatePath() const
{
    if (!m_tree.has_value() || m_currentStateNodeId < 0)
        return {};

    return m_tree->formatNodePath(m_currentStateNodeId, false);
}

std::string AnimatorComponent::getActiveMachinePath() const
{
    if (!m_tree.has_value() || m_currentStateNodeId < 0)
        return {};

    return m_tree->formatNodePath(m_currentStateNodeId, true);
}

float AnimatorComponent::getCurrentStateNormalizedTime() const
{
    return getNodeNormalizedTime(getCurrentLeafNode(), m_currentStateTimeSec);
}

bool AnimatorComponent::isInTransition() const { return m_nextStateNodeId >= 0; }

void AnimatorComponent::resetTreeRuntime()
{
    m_currentStateNodeId = -1;
    m_nextStateNodeId = -1;
    m_currentStatePath.clear();
    m_nextStatePath.clear();
    m_currentStateTimeSec = 0.0f;
    m_nextStateTimeSec = 0.0f;
    m_blendAlpha = 0.0f;
    m_blendDuration = 0.3f;
    m_transitionElapsed = 0.0f;
}

void AnimatorComponent::cacheTreeAnimations()
{
    m_treeClipAssets.clear();

    if (!m_tree.has_value())
        return;

    for (const auto &node : m_tree->graphNodes)
    {
        auto loadClipAsset = [this](const std::string &assetPath)
        {
            if (assetPath.empty() || m_treeClipAssets.find(assetPath) != m_treeClipAssets.end())
                return;

            auto animAsset = engine::AssetsLoader::loadAnimationAsset(assetPath);
            if (!animAsset.has_value() || animAsset->animations.empty())
                return;

            m_treeClipAssets.emplace(assetPath, std::move(animAsset->animations));
        };

        if (node.type == AnimationTreeNode::Type::ClipState)
        {
            loadClipAsset(node.animationAssetPath);
        }
        else if (node.type == AnimationTreeNode::Type::BlendSpace1D)
        {
            for (const auto &sample : node.blendSamples)
                loadClipAsset(sample.animationAssetPath);
        }
    }

    refreshAnimationBindings();
}

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

void AnimatorComponent::ensureTreeActivePath()
{
    if (!m_tree.has_value())
        return;

    if (getCurrentLeafNode() != nullptr)
        return;

    std::vector<int> resolvedPath;
    if (!resolveMachineEntryPath(m_tree->rootMachineNodeId, resolvedPath) || resolvedPath.empty())
        return;

    m_currentStatePath = std::move(resolvedPath);
    m_currentStateNodeId = m_currentStatePath.back();
    m_currentStateTimeSec = 0.0f;
}

bool AnimatorComponent::checkConditions(const std::vector<AnimationTransitionCondition> &conditions,
                                        const AnimationTreeNode *leafNode,
                                        float leafElapsedSeconds) const
{
    for (const auto &cond : conditions)
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
        case AnimationTransitionCondition::Type::FloatEqual:
        {
            const auto it = m_floats.find(cond.parameterName);
            if (it == m_floats.end() || std::abs(it->second - cond.floatThreshold) > kFloatConditionEqualityEpsilon)
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
        case AnimationTransitionCondition::Type::IntGreater:
        {
            const auto it = m_ints.find(cond.parameterName);
            if (it == m_ints.end() || !(it->second > cond.intValue))
                return false;
            break;
        }
        case AnimationTransitionCondition::Type::IntLess:
        {
            const auto it = m_ints.find(cond.parameterName);
            if (it == m_ints.end() || !(it->second < cond.intValue))
                return false;
            break;
        }
        case AnimationTransitionCondition::Type::Trigger:
        {
            if (m_triggers.find(cond.parameterName) == m_triggers.end())
                return false;
            break;
        }
        case AnimationTransitionCondition::Type::StateFinished:
        {
            if (!isNodeFinished(leafNode, leafElapsedSeconds))
                return false;
            break;
        }
        }
    }
    return true;
}

void AnimatorComponent::startTransition(const std::vector<int> &targetPath, float blendDuration)
{
    if (m_nextStateNodeId >= 0 || targetPath.empty())
        return;

    if (blendDuration <= 0.0f)
    {
        m_currentStatePath = targetPath;
        m_currentStateNodeId = targetPath.back();
        m_currentStateTimeSec = 0.0f;
        m_nextStatePath.clear();
        m_nextStateNodeId = -1;
        m_nextStateTimeSec = 0.0f;
        m_blendAlpha = 0.0f;
        m_transitionElapsed = 0.0f;
        return;
    }

    m_nextStatePath = targetPath;
    m_nextStateNodeId = targetPath.back();
    m_blendDuration = blendDuration;
    m_blendAlpha = 0.0f;
    m_transitionElapsed = 0.0f;
    m_nextStateTimeSec = 0.0f;
}

void AnimatorComponent::evaluateTransitions()
{
    if (!m_tree.has_value() || m_nextStateNodeId >= 0 || m_currentStatePath.size() < 2u)
        return;

    const AnimationTreeNode *currentLeaf = getCurrentLeafNode();
    if (!currentLeaf)
        return;

    const float currentNormalizedTime = getNodeNormalizedTime(currentLeaf, m_currentStateTimeSec);

    for (int pathIndex = static_cast<int>(m_currentStatePath.size()) - 2; pathIndex >= 0; --pathIndex)
    {
        const int machineNodeId = m_currentStatePath[static_cast<size_t>(pathIndex)];
        const AnimationTreeNode *machineNode = m_tree->findNode(machineNodeId);
        if (!machineNode || !machineNode->isStateMachine())
            continue;

        const int activeChildNodeId = m_currentStatePath[static_cast<size_t>(pathIndex + 1)];
        for (const auto &transition : machineNode->transitions)
        {
            const bool fromCurrent = transition.fromNodeId == activeChildNodeId;
            const bool fromAny = transition.fromNodeId == AnimationTree::ANY_NODE_ID;
            if (!fromCurrent && !fromAny)
                continue;
            if (transition.toNodeId == activeChildNodeId)
                continue;
            if (std::find(machineNode->childNodeIds.begin(),
                          machineNode->childNodeIds.end(),
                          transition.toNodeId) == machineNode->childNodeIds.end())
                continue;
            if (transition.hasExitTime && currentNormalizedTime < transition.exitTime)
                continue;
            if (!checkConditions(transition.conditions, currentLeaf, m_currentStateTimeSec))
                continue;

            std::vector<int> targetPath = buildTargetPath(machineNodeId, transition.toNodeId);
            if (targetPath.empty() || targetPath == m_currentStatePath)
                continue;

            startTransition(targetPath, transition.blendDuration);
            return;
        }
    }
}

float AnimatorComponent::secondsToTicks(const Animation *anim, float seconds) const
{
    if (!anim || anim->ticksPerSecond <= 0.0)
        return 0.0f;
    return seconds * static_cast<float>(anim->ticksPerSecond);
}

const Animation *AnimatorComponent::getAnimationClip(const std::string &assetPath, int clipIndex) const
{
    if (assetPath.empty())
        return nullptr;

    const auto it = m_treeClipAssets.find(assetPath);
    if (it == m_treeClipAssets.end() || it->second.empty())
        return nullptr;

    const int resolvedIndex = std::clamp(clipIndex, 0, static_cast<int>(it->second.size()) - 1);
    return &it->second[static_cast<size_t>(resolvedIndex)];
}

const AnimationTreeNode *AnimatorComponent::getCurrentLeafNode() const
{
    if (!m_tree.has_value() || m_currentStateNodeId < 0)
        return nullptr;

    const AnimationTreeNode *node = m_tree->findNode(m_currentStateNodeId);
    return (node && node->isLeaf()) ? node : nullptr;
}

const AnimationTreeNode *AnimatorComponent::getNextLeafNode() const
{
    if (!m_tree.has_value() || m_nextStateNodeId < 0)
        return nullptr;

    const AnimationTreeNode *node = m_tree->findNode(m_nextStateNodeId);
    return (node && node->isLeaf()) ? node : nullptr;
}

bool AnimatorComponent::resolveMachineEntryPath(int machineNodeId, std::vector<int> &outPath) const
{
    outPath.clear();
    return resolveNodePath(machineNodeId, outPath);
}

bool AnimatorComponent::resolveNodePath(int nodeId, std::vector<int> &outPath) const
{
    if (!m_tree.has_value())
        return false;

    const AnimationTreeNode *node = m_tree->findNode(nodeId);
    if (!node)
        return false;

    outPath.push_back(node->id);
    if (!node->isStateMachine())
        return true;

    int entryNodeId = node->entryNodeId;
    if (entryNodeId < 0 && !node->childNodeIds.empty())
        entryNodeId = node->childNodeIds.front();
    if (entryNodeId < 0)
        return false;

    return resolveNodePath(entryNodeId, outPath);
}

std::vector<int> AnimatorComponent::buildTargetPath(int machineNodeId, int targetNodeId) const
{
    std::vector<int> targetPath;
    const auto machineIt = std::find(m_currentStatePath.begin(), m_currentStatePath.end(), machineNodeId);
    if (machineIt == m_currentStatePath.end())
        return targetPath;

    targetPath.insert(targetPath.end(), m_currentStatePath.begin(), std::next(machineIt));
    std::vector<int> resolvedSuffix;
    if (!resolveNodePath(targetNodeId, resolvedSuffix))
        return {};

    targetPath.insert(targetPath.end(), resolvedSuffix.begin(), resolvedSuffix.end());
    return targetPath;
}

float AnimatorComponent::getNodeDurationSeconds(const AnimationTreeNode *node) const
{
    if (!node)
        return 0.0f;

    if (node->type == AnimationTreeNode::Type::ClipState)
        return getAnimationDurationSeconds(getAnimationClip(node->animationAssetPath, node->clipIndex));

    if (node->type == AnimationTreeNode::Type::BlendSpace1D)
    {
        float maxDuration = 0.0f;
        for (const auto &sample : node->blendSamples)
            maxDuration = std::max(maxDuration,
                                   getAnimationDurationSeconds(getAnimationClip(sample.animationAssetPath,
                                                                                sample.clipIndex)));
        return maxDuration;
    }

    return 0.0f;
}

float AnimatorComponent::getNodeNormalizedTime(const AnimationTreeNode *node, float elapsedSeconds) const
{
    if (!node)
        return 0.0f;

    const float durationSeconds = getNodeDurationSeconds(node);
    if (durationSeconds <= 0.0f)
        return 0.0f;

    if (node->loop)
    {
        const float wrapped = std::fmod(elapsedSeconds, durationSeconds);
        return (wrapped >= 0.0f ? wrapped : wrapped + durationSeconds) / durationSeconds;
    }

    return glm::clamp(elapsedSeconds / durationSeconds, 0.0f, 1.0f);
}

bool AnimatorComponent::isNodeFinished(const AnimationTreeNode *node, float elapsedSeconds) const
{
    if (!node || node->loop)
        return false;

    const float durationSeconds = getNodeDurationSeconds(node);
    return durationSeconds > 0.0f && elapsedSeconds >= durationSeconds;
}

void AnimatorComponent::buildPoseSource(const AnimationTreeNode *node, float elapsedSeconds, TreePoseSource &outPose) const
{
    outPose = {};
    if (!node)
        return;

    if (node->type == AnimationTreeNode::Type::ClipState)
    {
        outPose.primary = getAnimationClip(node->animationAssetPath, node->clipIndex);
        if (outPose.primary)
            outPose.primaryTicks = resolveStatePlaybackTicks(elapsedSeconds, outPose.primary, node->loop);
        return;
    }

    if (node->type != AnimationTreeNode::Type::BlendSpace1D || node->blendSamples.empty())
        return;

    std::vector<const AnimationBlendSpace1DSample *> sortedSamples;
    sortedSamples.reserve(node->blendSamples.size());
    for (const auto &sample : node->blendSamples)
    {
        if (getAnimationClip(sample.animationAssetPath, sample.clipIndex))
            sortedSamples.push_back(&sample);
    }

    if (sortedSamples.empty())
        return;

    std::sort(sortedSamples.begin(), sortedSamples.end(),
              [](const AnimationBlendSpace1DSample *lhs, const AnimationBlendSpace1DSample *rhs)
              { return lhs->position < rhs->position; });

    const float blendValue = getFloat(node->blendParameterName);
    const AnimationBlendSpace1DSample *left = sortedSamples.front();
    const AnimationBlendSpace1DSample *right = sortedSamples.front();

    if (blendValue <= sortedSamples.front()->position)
    {
        left = right = sortedSamples.front();
    }
    else if (blendValue >= sortedSamples.back()->position)
    {
        left = right = sortedSamples.back();
    }
    else
    {
        for (size_t sampleIndex = 1; sampleIndex < sortedSamples.size(); ++sampleIndex)
        {
            if (blendValue <= sortedSamples[sampleIndex]->position)
            {
                left = sortedSamples[sampleIndex - 1];
                right = sortedSamples[sampleIndex];
                break;
            }
        }
    }

    outPose.primary = getAnimationClip(left->animationAssetPath, left->clipIndex);
    if (outPose.primary)
        outPose.primaryTicks = resolveStatePlaybackTicks(elapsedSeconds, outPose.primary, node->loop);

    outPose.secondary = getAnimationClip(right->animationAssetPath, right->clipIndex);
    if (outPose.secondary)
        outPose.secondaryTicks = resolveStatePlaybackTicks(elapsedSeconds, outPose.secondary, node->loop);

    if (left != right)
    {
        const float denominator = right->position - left->position;
        outPose.sampleBlend = (std::abs(denominator) <= std::numeric_limits<float>::epsilon())
                                  ? 0.0f
                                  : glm::clamp((blendValue - left->position) / denominator, 0.0f, 1.0f);
    }
}

void AnimatorComponent::applyBlendedBoneTransform(Skeleton::BoneInfo *bone, const glm::mat4 &parentTransform,
                                                  const TreePoseSource &poseA,
                                                  const TreePoseSource *poseB,
                                                  const float blend)
{
    if (!bone)
        return;

    auto samplePoseSource = [bone](const TreePoseSource &source,
                                   glm::vec3 &outPos,
                                   glm::quat &outRot,
                                   glm::vec3 &outScale)
    {
        auto sampleClip = [bone](const Animation *animation,
                                 float ticks,
                                 glm::vec3 &ioPos,
                                 glm::quat &ioRot,
                                 glm::vec3 &ioScale)
        {
            if (!animation)
                return;

            if (const auto *track = animation->getAnimationTrack(bone->name); track && !track->keyFrames.empty())
            {
                auto [startFrame, endFrame] = findKeyframes(track->keyFrames, ticks);
                if (startFrame && endFrame)
                {
                    const float delta = endFrame->timeStamp - startFrame->timeStamp;
                    const float alpha = (delta == 0.0f) ? 0.0f : glm::clamp((ticks - startFrame->timeStamp) / delta, 0.0f, 1.0f);
                    ioPos = interpolateVec3(startFrame->position, endFrame->position, alpha);
                    ioRot = glm::normalize(interpolateQuat(startFrame->rotation, endFrame->rotation, alpha));
                    ioScale = interpolateVec3(startFrame->scale, endFrame->scale, alpha);
                }
            }
        };

        glm::vec3 primaryPos(bone->localBindTransform[3]);
        glm::quat primaryRot = glm::quat_cast(glm::mat3(bone->localBindTransform));
        glm::vec3 primaryScale(1.0f);
        sampleClip(source.primary, source.primaryTicks, primaryPos, primaryRot, primaryScale);

        if (!source.secondary)
        {
            outPos = primaryPos;
            outRot = primaryRot;
            outScale = primaryScale;
            return;
        }

        glm::vec3 secondaryPos(bone->localBindTransform[3]);
        glm::quat secondaryRot = glm::quat_cast(glm::mat3(bone->localBindTransform));
        glm::vec3 secondaryScale(1.0f);
        sampleClip(source.secondary, source.secondaryTicks, secondaryPos, secondaryRot, secondaryScale);

        outPos = interpolateVec3(primaryPos, secondaryPos, source.sampleBlend);
        outRot = glm::normalize(interpolateQuat(primaryRot, secondaryRot, source.sampleBlend));
        outScale = interpolateVec3(primaryScale, secondaryScale, source.sampleBlend);
    };

    glm::vec3 currentPos{};
    glm::quat currentRot{};
    glm::vec3 currentScale{};
    samplePoseSource(poseA, currentPos, currentRot, currentScale);

    const TreePoseSource &targetPose = poseB ? *poseB : poseA;
    glm::vec3 nextPos{};
    glm::quat nextRot{};
    glm::vec3 nextScale{};
    samplePoseSource(targetPose, nextPos, nextRot, nextScale);

    const float transitionBlend = poseB ? blend : 0.0f;
    const glm::vec3 pos = interpolateVec3(currentPos, nextPos, transitionBlend);
    const glm::quat rot = glm::normalize(interpolateQuat(currentRot, nextRot, transitionBlend));
    const glm::vec3 sca = interpolateVec3(currentScale, nextScale, transitionBlend);

    const glm::mat4 local = glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(rot) * glm::scale(glm::mat4(1.0f), sca);
    const glm::mat4 global = parentTransform * local;
    bone->finalTransformation = global;

    Skeleton *skel = m_boundSkeleton;
    if (!skel)
        skel = poseA.primary ? poseA.primary->skeletonForAnimation : nullptr;
    if (!skel)
        skel = poseA.secondary ? poseA.secondary->skeletonForAnimation : nullptr;
    if (!skel && poseB)
        skel = poseB->primary ? poseB->primary->skeletonForAnimation : nullptr;
    if (!skel && poseB)
        skel = poseB->secondary ? poseB->secondary->skeletonForAnimation : nullptr;
    if (!skel)
        return;

    for (const int childId : bone->children)
    {
        if (auto *child = skel->getBone(childId))
            applyBlendedBoneTransform(child, global, poseA, poseB, blend);
    }
}

void AnimatorComponent::applyTreePose()
{
    const AnimationTreeNode *currentLeaf = getCurrentLeafNode();
    if (!currentLeaf)
        return;

    TreePoseSource currentPose{};
    buildPoseSource(currentLeaf, m_currentStateTimeSec, currentPose);

    const AnimationTreeNode *nextLeaf = getNextLeafNode();
    TreePoseSource nextPose{};
    if (nextLeaf)
        buildPoseSource(nextLeaf, m_nextStateTimeSec, nextPose);

    Skeleton *skel = m_boundSkeleton;
    if (!skel)
        skel = currentPose.primary ? currentPose.primary->skeletonForAnimation : nullptr;
    if (!skel)
        skel = currentPose.secondary ? currentPose.secondary->skeletonForAnimation : nullptr;
    if (!skel && nextLeaf)
        skel = nextPose.primary ? nextPose.primary->skeletonForAnimation : nullptr;
    if (!skel && nextLeaf)
        skel = nextPose.secondary ? nextPose.secondary->skeletonForAnimation : nullptr;
    if (!skel)
        return;

    const glm::mat4 identity(1.0f);
    for (size_t i = 0; i < skel->getBonesCount(); ++i)
    {
        auto *root = skel->getBone(static_cast<int>(i));
        if (!root || root->parentId != -1)
            continue;
        applyBlendedBoneTransform(root, identity, currentPose, nextLeaf ? &nextPose : nullptr, m_blendAlpha);
    }
}

ELIX_NESTED_NAMESPACE_END
