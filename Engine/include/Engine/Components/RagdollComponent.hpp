#ifndef ELIX_RAGDOLL_COMPONENT_HPP
#define ELIX_RAGDOLL_COMPONENT_HPP

#include "Engine/Components/ECS.hpp"
#include "Engine/Physics/PhysXCore.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class AnimatorComponent;
class CharacterMovementComponent;
class Scene;
class SkeletalMeshComponent;
class Transform3DComponent;
class Skeleton;

enum class RagdollBodyShapeType : uint8_t
{
    Box = 0,
    Capsule = 1
};

struct RagdollBodyDesc
{
    std::string boneName;
    RagdollBodyShapeType shapeType{RagdollBodyShapeType::Capsule};
    glm::vec3 boxHalfExtents{0.08f, 0.12f, 0.08f};
    float capsuleRadius{0.06f};
    float capsuleHalfHeight{0.10f};
    glm::vec3 bodyLocalPosition{0.0f};
    glm::vec3 bodyLocalEulerDegrees{0.0f};
    float mass{1.0f};
    float linearDamping{0.05f};
    float angularDamping{0.05f};
};

struct RagdollJointDesc
{
    std::string parentBoneName;
    std::string childBoneName;
    float swingYLimitDeg{35.0f};
    float swingZLimitDeg{35.0f};
    float twistLowerLimitDeg{-30.0f};
    float twistUpperLimitDeg{30.0f};
    bool collisionEnabled{false};
};

struct RagdollProfile
{
    std::string referenceBoneName;
    std::vector<RagdollBodyDesc> bodies;
    std::vector<RagdollJointDesc> joints;
};

class RagdollComponent final : public ECS
{
public:
    enum class RuntimeState : uint8_t
    {
        Inactive = 0,
        Simulating = 1,
        Recovering = 2
    };

    explicit RagdollComponent(Scene *scene = nullptr);
    ~RagdollComponent() override = default;

    bool isSkeletonDriver() const override;

    void update(float deltaTime) override;
    void postPhysicsUpdate(float deltaTime) override;
    void onDetach() override;

    bool buildFromProfile();
    void autoGenerateHumanoidProfile();
    void enterRagdoll(bool preserveVelocity = true);
    void exitRagdoll(float blendTime = 0.25f);
    bool isSimulating() const;

    RagdollProfile &getProfile();
    const RagdollProfile &getProfile() const;

    void setDebugDrawBodies(bool enabled);
    bool getDebugDrawBodies() const;
    void setDebugDrawJoints(bool enabled);
    bool getDebugDrawJoints() const;

    RuntimeState getRuntimeState() const;

protected:
    void onOwnerAttached() override;

private:
    struct RuntimeBody
    {
        std::size_t profileIndex{0};
        int boneId{-1};
        int parentBoneId{-1};
        glm::mat4 boneLocalToBody{1.0f};
        glm::mat4 bodyLocalToBone{1.0f};
        glm::mat4 bindBodyModelTransform{1.0f};
        glm::vec3 shapeLocalPosition{0.0f};
        glm::quat shapeLocalRotation{1.0f, 0.0f, 0.0f, 0.0f};
        float referenceLength{0.0f};
        physx::PxRigidDynamic *actor{nullptr};
        physx::PxShape *shape{nullptr};
    };

    struct RuntimeJoint
    {
        std::size_t profileIndex{0};
        int parentBodyIndex{-1};
        int childBodyIndex{-1};
        glm::mat4 parentLocalFrame{1.0f};
        glm::mat4 childLocalFrame{1.0f};
        physx::PxD6Joint *joint{nullptr};
    };

    void refreshDependencies();
    void registerAnimatorHook();
    void unregisterAnimatorHook();
    void captureAnimatedPose(Skeleton &skeleton);
    bool rebuildRuntimeCache();
    void destroyPhysicsObjects();
    bool createPhysicsObjectsFromPose(const std::vector<glm::mat4> &modelPose, bool preserveVelocity);
    void syncEntityTransformFromReferenceBody();
    void writeSimulatedPoseToSkeleton();
    void writeRecoveryPoseToSkeleton();
    void drawDebug() const;

    const std::vector<glm::mat4> *resolveModelPoseForActivation() const;
    int getReferenceBodyIndex() const;

    Scene *m_scene{nullptr};
    SkeletalMeshComponent *m_skeletalMesh{nullptr};
    AnimatorComponent *m_animator{nullptr};
    CharacterMovementComponent *m_characterMovement{nullptr};
    Transform3DComponent *m_transform{nullptr};

    RagdollProfile m_profile;
    RuntimeState m_state{RuntimeState::Inactive};

    std::vector<RuntimeBody> m_runtimeBodies;
    std::vector<RuntimeJoint> m_runtimeJoints;
    std::unordered_map<int, int> m_bodyIndexByBoneId;

    std::vector<glm::mat4> m_cachedAnimatedPoseModel;
    std::vector<glm::mat4> m_previousAnimatedPoseModel;
    std::vector<glm::mat4> m_recoveryPoseModel;
    glm::mat4 m_referenceBoneModelAtActivation{1.0f};
    bool m_hasAnimatedPose{false};

    glm::vec3 m_lastObservedEntityPosition{0.0f};
    bool m_hasLastObservedEntityPosition{false};
    glm::vec3 m_estimatedEntityLinearVelocity{0.0f};

    float m_recoveryElapsed{0.0f};
    float m_recoveryDuration{0.25f};
    bool m_debugDrawBodies{false};
    bool m_debugDrawJoints{false};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_RAGDOLL_COMPONENT_HPP
