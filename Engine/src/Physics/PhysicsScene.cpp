#include "Engine/Physics/PhysicsScene.hpp"

#include <iostream>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

PhysicsScene::PhysicsScene(physx::PxPhysics *physics) : m_physics(physics)
{
    physx::PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = physx::PxDefaultCpuDispatcherCreate(2);
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
    m_scene = m_physics->createScene(sceneDesc);

    m_defaultMaterial = m_physics->createMaterial(0.5f, 0.5f, 0.6f);

    m_controllerManager = PxCreateControllerManager(*m_scene);
}

void PhysicsScene::update(float deltaTime)
{
    m_scene->simulate(deltaTime);
    m_scene->fetchResults(true);
}

void PhysicsScene::raycast()
{
}

physx::PxRigidDynamic *PhysicsScene::createDynamic(const physx::PxTransform &transform)
{
    auto dynamicBody = m_physics->createRigidDynamic(transform);
    dynamicBody->setMass(10.0f);
    m_scene->addActor(*dynamicBody);
    return dynamicBody;
}

void PhysicsScene::removeActor(physx::PxActor &actor, bool wakeOnLostTouch, bool release)
{
    m_scene->removeActor(actor, wakeOnLostTouch);

    if (release)
        actor.release();
}

physx::PxRigidStatic *PhysicsScene::createStatic(const physx::PxTransform &transform)
{
    auto staticBody = m_physics->createRigidStatic(transform);
    m_scene->addActor(*staticBody);

    return staticBody;
}

physx::PxShape *PhysicsScene::createShape(const physx::PxGeometry &geometry)
{
    auto shape = m_physics->createShape(geometry, *m_defaultMaterial);

    physx::PxFilterData filterData;
    filterData.word0 = 1;

    shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, true);
    shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
    // shape->setFlag(physx::PxShapeFlag::eVISUALIZATION, true);
    shape->setSimulationFilterData(filterData);
    shape->setQueryFilterData(filterData);

    return shape;
}

physx::PxController *PhysicsScene::createController(const physx::PxVec3 &position, float radius, float height)
{
    physx::PxCapsuleControllerDesc desc;
    desc.position = {position.x, position.y, position.z};
    desc.stepOffset = 0.0f;
    desc.material = m_defaultMaterial;
    desc.radius = radius;
    desc.height = height;

    physx::PxController *controller = m_controllerManager->createController(desc);

    // physx::PxRigidDynamic *rigidbody = controller->getActor();

    return controller;
}

PhysicsScene::~PhysicsScene()
{
    if (m_scene)
    {
        m_scene->release();
        m_scene = nullptr;
    }
}

ELIX_NESTED_NAMESPACE_END