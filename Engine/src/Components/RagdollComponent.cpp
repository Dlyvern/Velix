#include "Engine/Components/RagdollComponent.hpp"

#include "Engine/Components/AnimatorComponent.hpp"
#include "Engine/Components/CharacterMovementComponent.hpp"
#include "Engine/Components/SkeletalMeshComponent.hpp"
#include "Engine/Components/Transform3DComponent.hpp"
#include "Engine/DebugDraw.hpp"
#include "Engine/Entity.hpp"
#include "Engine/Physics/PhysicsScene.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Skeleton.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace
{
    using namespace elix::engine;

    constexpr float kEpsilon = 1e-5f;

    std::string toLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
                       { return static_cast<char>(std::tolower(character)); });
        return value;
    }

    glm::vec3 extractTranslation(const glm::mat4 &transform)
    {
        return glm::vec3(transform[3]);
    }

    glm::vec3 extractScale(const glm::mat4 &transform)
    {
        glm::vec3 scale(1.0f);
        scale.x = glm::length(glm::vec3(transform[0]));
        scale.y = glm::length(glm::vec3(transform[1]));
        scale.z = glm::length(glm::vec3(transform[2]));
        return glm::max(scale, glm::vec3(kEpsilon));
    }

    glm::quat extractRotation(const glm::mat4 &transform)
    {
        glm::mat3 basis(transform);
        const glm::vec3 scale = extractScale(transform);
        basis[0] /= scale.x;
        basis[1] /= scale.y;
        basis[2] /= scale.z;
        return glm::normalize(glm::quat_cast(basis));
    }

    glm::mat4 composeTransform(const glm::vec3 &position, const glm::quat &rotation)
    {
        return glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation);
    }

    glm::mat4 composeTransform(const glm::vec3 &position,
                               const glm::quat &rotation,
                               const glm::vec3 &scale)
    {
        return glm::translate(glm::mat4(1.0f), position) *
               glm::toMat4(rotation) *
               glm::scale(glm::mat4(1.0f), scale);
    }

    glm::vec3 transformPoint(const glm::mat4 &transform, const glm::vec3 &point)
    {
        return glm::vec3(transform * glm::vec4(point, 1.0f));
    }

    glm::quat safeRotationBetween(const glm::vec3 &from, const glm::vec3 &to)
    {
        const float fromLength = glm::length(from);
        const float toLength = glm::length(to);
        if (fromLength <= kEpsilon || toLength <= kEpsilon)
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        const glm::vec3 start = from / fromLength;
        const glm::vec3 end = to / toLength;
        const float dotProduct = glm::clamp(glm::dot(start, end), -1.0f, 1.0f);

        if (dotProduct > 1.0f - 1e-4f)
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        if (dotProduct < -1.0f + 1e-4f)
        {
            glm::vec3 axis = glm::cross(start, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::length2(axis) <= kEpsilon)
                axis = glm::cross(start, glm::vec3(1.0f, 0.0f, 0.0f));
            return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
        }

        return glm::normalize(glm::rotation(start, end));
    }

    physx::PxTransform toPxTransform(const glm::mat4 &transform)
    {
        const glm::vec3 position = extractTranslation(transform);
        const glm::quat rotation = extractRotation(transform);
        return physx::PxTransform(
            physx::PxVec3(position.x, position.y, position.z),
            physx::PxQuat(rotation.x, rotation.y, rotation.z, rotation.w));
    }

    physx::PxTransform toPxTransform(const glm::vec3 &position, const glm::quat &rotation)
    {
        return physx::PxTransform(
            physx::PxVec3(position.x, position.y, position.z),
            physx::PxQuat(rotation.x, rotation.y, rotation.z, rotation.w));
    }

    glm::mat4 fromPxTransform(const physx::PxTransform &transform)
    {
        return composeTransform(
            glm::vec3(transform.p.x, transform.p.y, transform.p.z),
            glm::quat(transform.q.w, transform.q.x, transform.q.y, transform.q.z));
    }

    int findBoneByAliases(const Skeleton &skeleton, const std::vector<std::string> &aliases)
    {
        for (const auto &alias : aliases)
        {
            const int exactId = skeleton.getBoneId(alias);
            if (exactId >= 0)
                return exactId;
        }

        for (size_t boneIndex = 0; boneIndex < skeleton.getBonesCount(); ++boneIndex)
        {
            const auto *bone = skeleton.getBone(static_cast<int>(boneIndex));
            if (!bone)
                continue;

            const std::string loweredName = toLowerCopy(bone->name);
            for (const auto &alias : aliases)
            {
                if (loweredName.find(toLowerCopy(alias)) != std::string::npos)
                    return bone->id;
            }
        }

        return -1;
    }

    int choosePrimaryChild(const Skeleton &skeleton, int boneId)
    {
        const auto *bone = skeleton.getBone(boneId);
        if (!bone || bone->children.empty())
            return -1;

        return bone->children.front();
    }

    float distanceBetweenBones(const Skeleton &skeleton, int boneId, int otherBoneId)
    {
        const auto *bone = skeleton.getBone(boneId);
        const auto *other = skeleton.getBone(otherBoneId);
        if (!bone || !other)
            return 0.0f;

        return glm::distance(extractTranslation(bone->globalBindTransform),
                             extractTranslation(other->globalBindTransform));
    }

    float fallbackLength(const Skeleton &skeleton, int boneId)
    {
        const auto *bone = skeleton.getBone(boneId);
        if (!bone)
            return 0.2f;

        if (bone->parentId >= 0)
        {
            const float parentDistance = distanceBetweenBones(skeleton, boneId, bone->parentId);
            if (parentDistance > kEpsilon)
                return parentDistance * 0.75f;
        }

        return 0.2f;
    }

    std::vector<glm::mat4> currentSkeletonModelPose(const Skeleton &skeleton)
    {
        std::vector<glm::mat4> result(skeleton.getBonesCount(), glm::mat4(1.0f));
        for (size_t boneIndex = 0; boneIndex < skeleton.getBonesCount(); ++boneIndex)
        {
            const auto *bone = skeleton.getBone(static_cast<int>(boneIndex));
            if (bone)
                result[boneIndex] = bone->finalTransformation;
        }
        return result;
    }

    glm::mat4 makeBodyOffsetMatrix(const RagdollBodyDesc &body)
    {
        return composeTransform(body.bodyLocalPosition, glm::quat(glm::radians(body.bodyLocalEulerDegrees)));
    }
}

ELIX_NESTED_NAMESPACE_BEGIN(engine)

RagdollComponent::RagdollComponent(Scene *scene)
    : m_scene(scene)
{
}

bool RagdollComponent::isSkeletonDriver() const
{
    return m_state == RuntimeState::Simulating || m_state == RuntimeState::Recovering;
}

void RagdollComponent::update(float deltaTime)
{
    refreshDependencies();

    if (m_transform)
    {
        const glm::vec3 worldPosition = m_transform->getWorldPosition();
        if (m_hasLastObservedEntityPosition && deltaTime > kEpsilon)
            m_estimatedEntityLinearVelocity = (worldPosition - m_lastObservedEntityPosition) / deltaTime;
        m_lastObservedEntityPosition = worldPosition;
        m_hasLastObservedEntityPosition = true;
    }

    if (m_state == RuntimeState::Recovering && m_animator && m_recoveryDuration > kEpsilon)
    {
        m_recoveryElapsed = std::min(m_recoveryElapsed + deltaTime, m_recoveryDuration);
        if (m_recoveryElapsed >= m_recoveryDuration)
            m_state = RuntimeState::Inactive;
    }
}

void RagdollComponent::postPhysicsUpdate(float /*deltaTime*/)
{
    refreshDependencies();

    if (!m_skeletalMesh)
        return;

    if (m_state == RuntimeState::Simulating)
    {
        syncEntityTransformFromReferenceBody();
        writeSimulatedPoseToSkeleton();
        drawDebug();
    }
    else if (m_state == RuntimeState::Recovering)
    {
        writeRecoveryPoseToSkeleton();
    }
}

void RagdollComponent::onDetach()
{
    unregisterAnimatorHook();
    destroyPhysicsObjects();
    if (m_characterMovement)
        m_characterMovement->setEnabled(true);
}

bool RagdollComponent::buildFromProfile()
{
    refreshDependencies();

    const bool shouldResumeSimulation = (m_state == RuntimeState::Simulating);
    const std::vector<glm::mat4> previousPose =
        m_skeletalMesh ? currentSkeletonModelPose(m_skeletalMesh->getSkeleton()) : std::vector<glm::mat4>{};

    destroyPhysicsObjects();
    if (!rebuildRuntimeCache())
    {
        m_state = RuntimeState::Inactive;
        return false;
    }

    if (shouldResumeSimulation && !previousPose.empty())
    {
        createPhysicsObjectsFromPose(previousPose, false);
        m_state = RuntimeState::Simulating;
    }

    return true;
}

void RagdollComponent::autoGenerateHumanoidProfile()
{
    refreshDependencies();
    if (!m_skeletalMesh)
        return;

    const Skeleton &skeleton = m_skeletalMesh->getSkeleton();
    if (skeleton.getBonesCount() == 0)
        return;

    struct AutoBodyTemplate
    {
        std::vector<std::string> aliases;
        RagdollBodyShapeType shapeType;
        float widthScale;
        float depthScale;
        float radiusScale;
        float halfHeightScale;
        float defaultLength;
        float mass;
        float swingY;
        float swingZ;
        float twistLower;
        float twistUpper;
    };

    const std::array<AutoBodyTemplate, 12> templates{{{{"pelvis", "hips", "hip"}, RagdollBodyShapeType::Box, 0.22f, 0.16f, 0.08f, 0.10f, 0.22f, 3.0f, 30.0f, 30.0f, -15.0f, 15.0f},
                                                      {{"spine_01", "spine01", "spine1", "spine"}, RagdollBodyShapeType::Box, 0.16f, 0.12f, 0.07f, 0.10f, 0.24f, 2.0f, 25.0f, 25.0f, -20.0f, 20.0f},
                                                      {{"spine_02", "spine02", "spine2", "chest", "upperchest"}, RagdollBodyShapeType::Box, 0.18f, 0.14f, 0.08f, 0.12f, 0.28f, 2.5f, 30.0f, 30.0f, -25.0f, 25.0f},
                                                      {{"head", "neck_01", "neck01", "neck"}, RagdollBodyShapeType::Box, 0.12f, 0.12f, 0.06f, 0.08f, 0.20f, 1.5f, 35.0f, 35.0f, -40.0f, 40.0f},
                                                      {{"upperarm_l", "leftarm", "leftupperarm", "l_upperarm", "arm_l"}, RagdollBodyShapeType::Capsule, 0.08f, 0.08f, 0.20f, 0.45f, 0.28f, 1.0f, 85.0f, 85.0f, -90.0f, 90.0f},
                                                      {{"lowerarm_l", "leftforearm", "leftlowerarm", "l_forearm", "forearm_l"}, RagdollBodyShapeType::Capsule, 0.07f, 0.07f, 0.18f, 0.45f, 0.26f, 0.8f, 10.0f, 95.0f, -10.0f, 10.0f},
                                                      {{"upperarm_r", "rightarm", "rightupperarm", "r_upperarm", "arm_r"}, RagdollBodyShapeType::Capsule, 0.08f, 0.08f, 0.20f, 0.45f, 0.28f, 1.0f, 85.0f, 85.0f, -90.0f, 90.0f},
                                                      {{"lowerarm_r", "rightforearm", "rightlowerarm", "r_forearm", "forearm_r"}, RagdollBodyShapeType::Capsule, 0.07f, 0.07f, 0.18f, 0.45f, 0.26f, 0.8f, 10.0f, 95.0f, -10.0f, 10.0f},
                                                      {{"thigh_l", "upleg_l", "leftupleg", "leftthigh", "leg_l"}, RagdollBodyShapeType::Capsule, 0.09f, 0.09f, 0.22f, 0.48f, 0.42f, 1.8f, 55.0f, 35.0f, -35.0f, 25.0f},
                                                      {{"calf_l", "leg_l", "leftleg", "leftcalf", "lowerleg_l"}, RagdollBodyShapeType::Capsule, 0.08f, 0.08f, 0.18f, 0.48f, 0.38f, 1.4f, 5.0f, 90.0f, -5.0f, 5.0f},
                                                      {{"thigh_r", "upleg_r", "rightupleg", "rightthigh", "leg_r"}, RagdollBodyShapeType::Capsule, 0.09f, 0.09f, 0.22f, 0.48f, 0.42f, 1.8f, 55.0f, 35.0f, -25.0f, 35.0f},
                                                      {{"calf_r", "rightleg", "rightcalf", "lowerleg_r", "r_calf"}, RagdollBodyShapeType::Capsule, 0.08f, 0.08f, 0.18f, 0.48f, 0.38f, 1.4f, 5.0f, 90.0f, -5.0f, 5.0f}}};

    m_profile = {};
    std::unordered_map<int, RagdollJointDesc> jointsByChildBoneId;

    for (const AutoBodyTemplate &entry : templates)
    {
        const int boneId = findBoneByAliases(skeleton, entry.aliases);
        if (boneId < 0)
            continue;

        const auto *bone = skeleton.getBone(boneId);
        if (!bone)
            continue;

        const int childBoneId = choosePrimaryChild(skeleton, boneId);
        float length = childBoneId >= 0 ? distanceBetweenBones(skeleton, boneId, childBoneId) : 0.0f;
        if (length <= kEpsilon)
            length = fallbackLength(skeleton, boneId);
        length = std::max(length, entry.defaultLength);

        RagdollBodyDesc body{};
        body.boneName = bone->name;
        body.shapeType = entry.shapeType;
        body.mass = entry.mass;
        body.linearDamping = 0.08f;
        body.angularDamping = 0.08f;
        if (entry.shapeType == RagdollBodyShapeType::Box)
        {
            body.boxHalfExtents = glm::vec3(
                std::max(0.04f, length * entry.widthScale),
                std::max(0.05f, length * 0.40f),
                std::max(0.04f, length * entry.depthScale));
        }
        else
        {
            body.capsuleRadius = std::max(0.04f, length * entry.radiusScale);
            body.capsuleHalfHeight = std::max(0.02f, length * entry.halfHeightScale * 0.5f);
        }
        m_profile.bodies.push_back(body);

        if (bone->parentId >= 0)
        {
            const auto *parentBone = skeleton.getBone(bone->parentId);
            if (parentBone)
            {
                RagdollJointDesc joint{};
                joint.parentBoneName = parentBone->name;
                joint.childBoneName = bone->name;
                joint.swingYLimitDeg = entry.swingY;
                joint.swingZLimitDeg = entry.swingZ;
                joint.twistLowerLimitDeg = entry.twistLower;
                joint.twistUpperLimitDeg = entry.twistUpper;
                jointsByChildBoneId[boneId] = std::move(joint);
            }
        }
    }

    const int pelvisBoneId = findBoneByAliases(skeleton, {"pelvis", "hips", "hip"});
    if (pelvisBoneId >= 0)
    {
        if (const auto *pelvisBone = skeleton.getBone(pelvisBoneId))
            m_profile.referenceBoneName = pelvisBone->name;
    }
    else if (!m_profile.bodies.empty())
    {
        m_profile.referenceBoneName = m_profile.bodies.front().boneName;
    }

    for (const auto &[boneId, joint] : jointsByChildBoneId)
    {
        (void)boneId;
        m_profile.joints.push_back(joint);
    }

    buildFromProfile();
}

void RagdollComponent::enterRagdoll(bool preserveVelocity)
{
    refreshDependencies();
    if (!m_scene || !m_transform || !m_skeletalMesh)
        return;

    if (m_runtimeBodies.empty() && !buildFromProfile())
        return;

    const std::vector<glm::mat4> *pose = resolveModelPoseForActivation();
    if (!pose || pose->empty())
        return;

    destroyPhysicsObjects();
    if (!createPhysicsObjectsFromPose(*pose, preserveVelocity))
        return;

    if (m_characterMovement)
        m_characterMovement->setEnabled(false);

    const int referenceBodyIndex = getReferenceBodyIndex();
    if (referenceBodyIndex >= 0 && referenceBodyIndex < static_cast<int>(m_runtimeBodies.size()))
    {
        const RuntimeBody &referenceBody = m_runtimeBodies[referenceBodyIndex];
        if (referenceBody.boneId >= 0 && referenceBody.boneId < static_cast<int>(pose->size()))
            m_referenceBoneModelAtActivation = (*pose)[referenceBody.boneId];
    }

    m_state = RuntimeState::Simulating;
    syncEntityTransformFromReferenceBody();
    writeSimulatedPoseToSkeleton();
}

void RagdollComponent::exitRagdoll(float blendTime)
{
    if (m_state != RuntimeState::Simulating || !m_skeletalMesh)
        return;

    m_recoveryPoseModel = currentSkeletonModelPose(m_skeletalMesh->getSkeleton());
    destroyPhysicsObjects();

    if (m_characterMovement)
        m_characterMovement->setEnabled(true);

    m_recoveryElapsed = 0.0f;
    m_recoveryDuration = std::max(blendTime, 0.0f);
    m_state = RuntimeState::Recovering;
}

bool RagdollComponent::isSimulating() const
{
    return m_state == RuntimeState::Simulating;
}

RagdollProfile &RagdollComponent::getProfile()
{
    return m_profile;
}

const RagdollProfile &RagdollComponent::getProfile() const
{
    return m_profile;
}

void RagdollComponent::setDebugDrawBodies(bool enabled)
{
    m_debugDrawBodies = enabled;
}

bool RagdollComponent::getDebugDrawBodies() const
{
    return m_debugDrawBodies;
}

void RagdollComponent::setDebugDrawJoints(bool enabled)
{
    m_debugDrawJoints = enabled;
}

bool RagdollComponent::getDebugDrawJoints() const
{
    return m_debugDrawJoints;
}

RagdollComponent::RuntimeState RagdollComponent::getRuntimeState() const
{
    return m_state;
}

void RagdollComponent::onOwnerAttached()
{
    refreshDependencies();
    registerAnimatorHook();
}

void RagdollComponent::refreshDependencies()
{
    auto *owner = getOwner<Entity>();
    if (!owner)
        return;

    m_skeletalMesh = owner->getComponent<SkeletalMeshComponent>();
    m_characterMovement = owner->getComponent<CharacterMovementComponent>();
    m_transform = owner->getComponent<Transform3DComponent>();

    auto *currentAnimator = owner->getComponent<AnimatorComponent>();
    if (currentAnimator != m_animator)
    {
        m_animator = currentAnimator;
        registerAnimatorHook();
    }
}

void RagdollComponent::registerAnimatorHook()
{
    unregisterAnimatorHook();

    if (!m_animator)
        return;

    m_animator->addPostAnimHook(this, [this](Skeleton &skeleton)
                                { captureAnimatedPose(skeleton); });
}

void RagdollComponent::unregisterAnimatorHook()
{
    if (m_animator)
        m_animator->removePostAnimHook(this);
}

void RagdollComponent::captureAnimatedPose(Skeleton &skeleton)
{
    m_previousAnimatedPoseModel = m_cachedAnimatedPoseModel;
    m_cachedAnimatedPoseModel = currentSkeletonModelPose(skeleton);
    m_hasAnimatedPose = !m_cachedAnimatedPoseModel.empty();
}

bool RagdollComponent::rebuildRuntimeCache()
{
    m_runtimeBodies.clear();
    m_runtimeJoints.clear();
    m_bodyIndexByBoneId.clear();

    if (!m_skeletalMesh)
        return false;

    Skeleton &skeleton = m_skeletalMesh->getSkeleton();
    if (skeleton.getBonesCount() == 0 || m_profile.bodies.empty())
        return false;

    for (std::size_t profileIndex = 0; profileIndex < m_profile.bodies.size(); ++profileIndex)
    {
        const RagdollBodyDesc &bodyDesc = m_profile.bodies[profileIndex];
        const int boneId = skeleton.getBoneId(bodyDesc.boneName);
        if (boneId < 0 || m_bodyIndexByBoneId.contains(boneId))
            continue;

        const auto *bone = skeleton.getBone(boneId);
        if (!bone)
            continue;

        RuntimeBody runtimeBody{};
        runtimeBody.profileIndex = profileIndex;
        runtimeBody.boneId = boneId;
        runtimeBody.parentBoneId = bone->parentId;
        runtimeBody.boneLocalToBody = makeBodyOffsetMatrix(bodyDesc);
        runtimeBody.bodyLocalToBone = glm::inverse(runtimeBody.boneLocalToBody);
        runtimeBody.bindBodyModelTransform = bone->globalBindTransform * runtimeBody.boneLocalToBody;

        const int childBoneId = choosePrimaryChild(skeleton, boneId);
        const glm::vec3 bonePosition = extractTranslation(bone->globalBindTransform);
        glm::vec3 localDirection(0.0f, 1.0f, 0.0f);
        float referenceLength = fallbackLength(skeleton, boneId);
        if (childBoneId >= 0)
        {
            if (const auto *childBone = skeleton.getBone(childBoneId))
            {
                const glm::vec3 childPosition = extractTranslation(childBone->globalBindTransform);
                referenceLength = std::max(glm::distance(bonePosition, childPosition), kEpsilon);
                const glm::mat4 bodyBindInverse = glm::inverse(runtimeBody.bindBodyModelTransform);
                localDirection = glm::normalize(glm::vec3(bodyBindInverse * glm::vec4(childPosition, 1.0f)));
            }
        }

        runtimeBody.referenceLength = referenceLength;
        if (bodyDesc.shapeType == RagdollBodyShapeType::Capsule)
        {
            runtimeBody.shapeLocalRotation = safeRotationBetween(glm::vec3(1.0f, 0.0f, 0.0f), localDirection);
            runtimeBody.shapeLocalPosition = localDirection * referenceLength * 0.5f;
        }
        else
        {
            runtimeBody.shapeLocalRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            runtimeBody.shapeLocalPosition = localDirection * referenceLength * 0.35f;
        }

        m_bodyIndexByBoneId[boneId] = static_cast<int>(m_runtimeBodies.size());
        m_runtimeBodies.push_back(runtimeBody);
    }

    for (std::size_t profileIndex = 0; profileIndex < m_profile.joints.size(); ++profileIndex)
    {
        const RagdollJointDesc &jointDesc = m_profile.joints[profileIndex];
        const int parentBoneId = skeleton.getBoneId(jointDesc.parentBoneName);
        const int childBoneId = skeleton.getBoneId(jointDesc.childBoneName);
        if (parentBoneId < 0 || childBoneId < 0)
            continue;

        const auto parentBodyIt = m_bodyIndexByBoneId.find(parentBoneId);
        const auto childBodyIt = m_bodyIndexByBoneId.find(childBoneId);
        if (parentBodyIt == m_bodyIndexByBoneId.end() || childBodyIt == m_bodyIndexByBoneId.end())
            continue;

        const auto *childBone = skeleton.getBone(childBoneId);
        if (!childBone)
            continue;

        RuntimeJoint runtimeJoint{};
        runtimeJoint.profileIndex = profileIndex;
        runtimeJoint.parentBodyIndex = parentBodyIt->second;
        runtimeJoint.childBodyIndex = childBodyIt->second;
        runtimeJoint.parentLocalFrame =
            glm::inverse(m_runtimeBodies[runtimeJoint.parentBodyIndex].bindBodyModelTransform) * childBone->globalBindTransform;
        runtimeJoint.childLocalFrame =
            glm::inverse(m_runtimeBodies[runtimeJoint.childBodyIndex].bindBodyModelTransform) * childBone->globalBindTransform;
        m_runtimeJoints.push_back(runtimeJoint);
    }

    return !m_runtimeBodies.empty();
}

void RagdollComponent::destroyPhysicsObjects()
{
    if (!m_scene)
        return;

    PhysicsScene &physicsScene = m_scene->getPhysicsScene();
    for (auto &runtimeJoint : m_runtimeJoints)
    {
        if (runtimeJoint.joint)
        {
            physicsScene.removeJoint(*runtimeJoint.joint, true);
            runtimeJoint.joint = nullptr;
        }
    }

    for (auto &runtimeBody : m_runtimeBodies)
    {
        if (runtimeBody.actor)
        {
            physicsScene.removeActor(*runtimeBody.actor, true, true);
            runtimeBody.actor = nullptr;
            runtimeBody.shape = nullptr;
        }
    }
}

bool RagdollComponent::createPhysicsObjectsFromPose(const std::vector<glm::mat4> &modelPose, bool preserveVelocity)
{
    if (!m_scene || !m_transform)
        return false;

    PhysicsScene &physicsScene = m_scene->getPhysicsScene();
    const glm::mat4 entityWorld = m_transform->getMatrix();

    for (auto &runtimeBody : m_runtimeBodies)
    {
        if (runtimeBody.boneId < 0 || runtimeBody.boneId >= static_cast<int>(modelPose.size()))
            continue;

        const RagdollBodyDesc &bodyDesc = m_profile.bodies[runtimeBody.profileIndex];
        const glm::mat4 bodyWorldTransform = entityWorld * modelPose[runtimeBody.boneId] * runtimeBody.boneLocalToBody;
        auto *actor = physicsScene.createDynamic(toPxTransform(bodyWorldTransform));
        if (!actor)
            return false;

        actor->userData = getOwner<Entity>();
        actor->setLinearDamping(std::max(bodyDesc.linearDamping, 0.0f));
        actor->setAngularDamping(std::max(bodyDesc.angularDamping, 0.0f));

        const glm::vec3 bodyScale = extractScale(bodyWorldTransform);
        physx::PxShape *shape = nullptr;
        if (bodyDesc.shapeType == RagdollBodyShapeType::Box)
        {
            const glm::vec3 halfExtents = glm::max(bodyDesc.boxHalfExtents * bodyScale, glm::vec3(0.01f));
            shape = physicsScene.createShape(physx::PxBoxGeometry(halfExtents.x, halfExtents.y, halfExtents.z));
        }
        else
        {
            const float uniformScale = std::max({bodyScale.x, bodyScale.y, bodyScale.z, 0.01f});
            const float radius = std::max(bodyDesc.capsuleRadius * uniformScale, 0.01f);
            const float halfHeight = std::max(bodyDesc.capsuleHalfHeight * uniformScale, 0.0f);
            shape = physicsScene.createShape(physx::PxCapsuleGeometry(radius, halfHeight));
        }

        if (!shape)
        {
            physicsScene.removeActor(*actor, true, true);
            return false;
        }

        shape->setLocalPose(toPxTransform(runtimeBody.shapeLocalPosition, runtimeBody.shapeLocalRotation));
        actor->attachShape(*shape);
        physx::PxRigidBodyExt::setMassAndUpdateInertia(*actor, std::max(bodyDesc.mass, 0.05f));

        if (preserveVelocity)
        {
            actor->setLinearVelocity(physx::PxVec3(m_estimatedEntityLinearVelocity.x,
                                                   m_estimatedEntityLinearVelocity.y,
                                                   m_estimatedEntityLinearVelocity.z));
        }

        runtimeBody.actor = actor;
        runtimeBody.shape = shape;
    }

    for (auto &runtimeJoint : m_runtimeJoints)
    {
        if (runtimeJoint.parentBodyIndex < 0 || runtimeJoint.childBodyIndex < 0)
            continue;

        RuntimeBody &parentBody = m_runtimeBodies[runtimeJoint.parentBodyIndex];
        RuntimeBody &childBody = m_runtimeBodies[runtimeJoint.childBodyIndex];
        if (!parentBody.actor || !childBody.actor)
            continue;

        auto *joint = physicsScene.createD6Joint(parentBody.actor,
                                                 toPxTransform(runtimeJoint.parentLocalFrame),
                                                 childBody.actor,
                                                 toPxTransform(runtimeJoint.childLocalFrame));
        if (!joint)
            continue;

        const RagdollJointDesc &jointDesc = m_profile.joints[runtimeJoint.profileIndex];
        joint->setMotion(physx::PxD6Axis::eX, physx::PxD6Motion::eLOCKED);
        joint->setMotion(physx::PxD6Axis::eY, physx::PxD6Motion::eLOCKED);
        joint->setMotion(physx::PxD6Axis::eZ, physx::PxD6Motion::eLOCKED);
        joint->setMotion(physx::PxD6Axis::eTWIST, physx::PxD6Motion::eLIMITED);
        joint->setMotion(physx::PxD6Axis::eSWING1, physx::PxD6Motion::eLIMITED);
        joint->setMotion(physx::PxD6Axis::eSWING2, physx::PxD6Motion::eLIMITED);
        joint->setTwistLimit(physx::PxJointAngularLimitPair(
            glm::radians(jointDesc.twistLowerLimitDeg),
            glm::radians(jointDesc.twistUpperLimitDeg)));
        joint->setSwingLimit(physx::PxJointLimitCone(
            glm::radians(std::max(jointDesc.swingYLimitDeg, 0.0f)),
            glm::radians(std::max(jointDesc.swingZLimitDeg, 0.0f))));
        joint->setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, jointDesc.collisionEnabled);
        runtimeJoint.joint = joint;
    }

    return true;
}

void RagdollComponent::syncEntityTransformFromReferenceBody()
{
    if (!m_transform)
        return;

    const int referenceBodyIndex = getReferenceBodyIndex();
    if (referenceBodyIndex < 0 || referenceBodyIndex >= static_cast<int>(m_runtimeBodies.size()))
        return;

    const RuntimeBody &referenceBody = m_runtimeBodies[referenceBodyIndex];
    if (!referenceBody.actor)
        return;

    const glm::mat4 referenceBodyWorld = fromPxTransform(referenceBody.actor->getGlobalPose());
    const glm::mat4 referenceBoneWorld = referenceBodyWorld * referenceBody.bodyLocalToBone;
    const glm::mat4 entityWorld = referenceBoneWorld * glm::inverse(m_referenceBoneModelAtActivation);
    m_transform->setWorldPosition(extractTranslation(entityWorld));
    m_transform->setWorldRotation(extractRotation(entityWorld));
}

void RagdollComponent::writeSimulatedPoseToSkeleton()
{
    if (!m_skeletalMesh || !m_transform)
        return;

    Skeleton &skeleton = m_skeletalMesh->getSkeleton();
    const glm::mat4 inverseEntityWorld = glm::inverse(m_transform->getMatrix());

    for (const auto &runtimeBody : m_runtimeBodies)
    {
        if (!runtimeBody.actor)
            continue;

        auto *bone = skeleton.getBone(runtimeBody.boneId);
        if (!bone)
            continue;

        const glm::mat4 bodyWorld = fromPxTransform(runtimeBody.actor->getGlobalPose());
        const glm::mat4 boneWorld = bodyWorld * runtimeBody.bodyLocalToBone;
        const glm::mat4 boneModel = inverseEntityWorld * boneWorld;
        const glm::vec3 bindScale = extractScale(bone->localBindTransform);

        bone->finalTransformation = composeTransform(
            extractTranslation(boneModel),
            extractRotation(boneModel),
            bindScale);
    }
}

void RagdollComponent::writeRecoveryPoseToSkeleton()
{
    if (!m_skeletalMesh || m_recoveryPoseModel.empty())
        return;

    Skeleton &skeleton = m_skeletalMesh->getSkeleton();
    std::vector<glm::mat4> animationPose = m_hasAnimatedPose ? m_cachedAnimatedPoseModel : currentSkeletonModelPose(skeleton);
    if (animationPose.size() < skeleton.getBonesCount())
        animationPose.resize(skeleton.getBonesCount(), glm::mat4(1.0f));

    float alpha = 0.0f;
    if (m_animator && m_recoveryDuration > kEpsilon)
        alpha = glm::clamp(m_recoveryElapsed / m_recoveryDuration, 0.0f, 1.0f);

    for (size_t boneIndex = 0; boneIndex < skeleton.getBonesCount(); ++boneIndex)
    {
        auto *bone = skeleton.getBone(static_cast<int>(boneIndex));
        if (!bone)
            continue;

        const glm::mat4 recoveryTransform =
            boneIndex < m_recoveryPoseModel.size() ? m_recoveryPoseModel[boneIndex] : bone->globalBindTransform;
        const glm::mat4 animationTransform = animationPose[boneIndex];
        const glm::vec3 bindScale = extractScale(bone->localBindTransform);

        bone->finalTransformation = composeTransform(
            glm::mix(extractTranslation(recoveryTransform), extractTranslation(animationTransform), alpha),
            glm::normalize(glm::slerp(extractRotation(recoveryTransform), extractRotation(animationTransform), alpha)),
            bindScale);
    }
}

void RagdollComponent::drawDebug() const
{
    if (!m_debugDrawBodies && !m_debugDrawJoints)
        return;

    if (m_debugDrawBodies)
    {
        for (const auto &runtimeBody : m_runtimeBodies)
        {
            if (!runtimeBody.actor)
                continue;

            const RagdollBodyDesc &bodyDesc = m_profile.bodies[runtimeBody.profileIndex];
            const glm::mat4 bodyWorld = fromPxTransform(runtimeBody.actor->getGlobalPose());
            const glm::mat4 shapeWorld = bodyWorld * composeTransform(runtimeBody.shapeLocalPosition, runtimeBody.shapeLocalRotation);

            if (bodyDesc.shapeType == RagdollBodyShapeType::Box)
            {
                DebugDraw::box(shapeWorld, bodyDesc.boxHalfExtents, glm::vec4(0.25f, 0.9f, 0.35f, 1.0f), 0.0f);
            }
            else
            {
                const glm::vec3 axis = glm::rotate(runtimeBody.shapeLocalRotation, glm::vec3(1.0f, 0.0f, 0.0f));
                const glm::vec3 center = transformPoint(bodyWorld, runtimeBody.shapeLocalPosition);
                const glm::vec3 offset = axis * bodyDesc.capsuleHalfHeight;
                DebugDraw::capsule(center - offset, center + offset, bodyDesc.capsuleRadius, glm::vec4(0.25f, 0.9f, 0.35f, 1.0f), 0.0f);
            }
        }
    }

    if (m_debugDrawJoints)
    {
        for (const auto &runtimeJoint : m_runtimeJoints)
        {
            if (!runtimeJoint.joint ||
                runtimeJoint.parentBodyIndex < 0 ||
                runtimeJoint.childBodyIndex < 0)
                continue;

            const RuntimeBody &parentBody = m_runtimeBodies[runtimeJoint.parentBodyIndex];
            const RuntimeBody &childBody = m_runtimeBodies[runtimeJoint.childBodyIndex];
            if (!parentBody.actor || !childBody.actor)
                continue;

            const glm::mat4 parentWorld = fromPxTransform(parentBody.actor->getGlobalPose());
            const glm::mat4 childWorld = fromPxTransform(childBody.actor->getGlobalPose());
            const glm::vec3 parentAnchor = extractTranslation(parentWorld * runtimeJoint.parentLocalFrame);
            const glm::vec3 childAnchor = extractTranslation(childWorld * runtimeJoint.childLocalFrame);
            DebugDraw::line(parentAnchor, childAnchor, glm::vec4(1.0f, 0.72f, 0.2f, 1.0f), 0.0f);
            DebugDraw::cross(childAnchor, 0.03f, 0.0f);
        }
    }
}

const std::vector<glm::mat4> *RagdollComponent::resolveModelPoseForActivation() const
{
    if (m_hasAnimatedPose && !m_cachedAnimatedPoseModel.empty())
        return &m_cachedAnimatedPoseModel;

    if (!m_skeletalMesh)
        return nullptr;

    static thread_local std::vector<glm::mat4> fallbackPose;
    fallbackPose = currentSkeletonModelPose(m_skeletalMesh->getSkeleton());
    return fallbackPose.empty() ? nullptr : &fallbackPose;
}

int RagdollComponent::getReferenceBodyIndex() const
{
    if (!m_skeletalMesh)
        return m_runtimeBodies.empty() ? -1 : 0;

    const int referenceBoneId = m_skeletalMesh->getSkeleton().getBoneId(m_profile.referenceBoneName);
    if (referenceBoneId >= 0)
    {
        const auto it = m_bodyIndexByBoneId.find(referenceBoneId);
        if (it != m_bodyIndexByBoneId.end())
            return it->second;
    }

    return m_runtimeBodies.empty() ? -1 : 0;
}

ELIX_NESTED_NAMESPACE_END
