#ifndef ELIX_OBJECT_ID_ENCODING_HPP
#define ELIX_OBJECT_ID_ENCODING_HPP

#include "Core/Macros.hpp"

#include <algorithm>
#include <cstdint>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(render)

constexpr uint32_t OBJECT_ID_MESH_BITS = 10u;
constexpr uint32_t OBJECT_ID_MESH_MASK = (1u << OBJECT_ID_MESH_BITS) - 1u;
constexpr uint32_t OBJECT_ID_ENTITY_SHIFT = OBJECT_ID_MESH_BITS;
constexpr uint32_t OBJECT_ID_NONE = 0u;
constexpr uint32_t OBJECT_ID_MAX_MESH_SLOT = OBJECT_ID_MESH_MASK - 1u;

inline uint32_t encodeObjectId(uint32_t entityId, uint32_t meshSlot)
{
    const uint32_t encodedEntity = entityId + 1u;
    const uint32_t encodedMesh = std::min(meshSlot, OBJECT_ID_MAX_MESH_SLOT) + 1u;
    return (encodedEntity << OBJECT_ID_ENTITY_SHIFT) | encodedMesh;
}

inline uint32_t decodeEntityEncoded(uint32_t encodedObjectId)
{
    return encodedObjectId >> OBJECT_ID_ENTITY_SHIFT;
}

inline uint32_t decodeMeshEncoded(uint32_t encodedObjectId)
{
    return encodedObjectId & OBJECT_ID_MESH_MASK;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_OBJECT_ID_ENCODING_HPP
