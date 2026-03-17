#include "Engine/Particles/Modules/CollisionModule.hpp"
#include "Engine/Particles/ParticleTypes.hpp"

#include <glm/gtc/epsilon.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

void CollisionModule::onParticleUpdate(Particle &p, float dt)
{
    if (!m_physicsScene)
        return;

    const float speed = glm::length(p.velocity);
    if (speed < 1e-4f)
        return;

    const glm::vec3 dir = p.velocity / speed;
    const float castDist = speed * dt * lookAheadMultiplier;

    PhysicsRaycastHit hit;
    if (!m_physicsScene->raycast(p.position, dir, castDist, &hit))
        return;

    p.position = hit.position;
    p.age = p.lifetime;
    m_hitPositions.push_back(hit.position);
}

ELIX_NESTED_NAMESPACE_END
