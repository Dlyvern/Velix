#version 450

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform usampler2D uObjectId;

layout(push_constant) uniform SelectionOverlayPC
{
    uint selectedEntityId;
    uint selectedMeshSlot;
    float outlineMix;
    float _padding0;
    vec4 outlineColor;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

const uint OBJECT_ID_MESH_BITS = 10u;
const uint OBJECT_ID_MESH_MASK = (1u << OBJECT_ID_MESH_BITS) - 1u;

uint decodeEntityId(uint objectId)
{
    return objectId >> OBJECT_ID_MESH_BITS;
}

uint decodeMeshSlot(uint objectId)
{
    return objectId & OBJECT_ID_MESH_MASK;
}

bool isSelectedObject(uint objectId)
{
    if (pc.selectedEntityId == 0u)
        return false;

    if (decodeEntityId(objectId) != pc.selectedEntityId)
        return false;

    if (pc.selectedMeshSlot == 0u)
        return true;

    return decodeMeshSlot(objectId) == pc.selectedMeshSlot;
}

bool hasSelectedNeighbor(ivec2 pixelCoord, ivec2 texSize)
{
    const ivec2 offsets[4] = ivec2[4](
        ivec2(-1, 0),
        ivec2(1, 0),
        ivec2(0, -1),
        ivec2(0, 1)
    );

    for (int i = 0; i < 4; ++i)
    {
        ivec2 p = clamp(pixelCoord + offsets[i], ivec2(0), texSize - ivec2(1));
        uint id = texelFetch(uObjectId, p, 0).r;

        if (isSelectedObject(id))
            return true;
    }

    return false;
}

void main()
{
    vec4 baseColor = texture(uSceneColor, vUV);
    outColor = baseColor;

    if (pc.selectedEntityId == 0u)
        return;

    ivec2 texSize = textureSize(uObjectId, 0);
    ivec2 pixelCoord = clamp(ivec2(vUV * vec2(texSize)), ivec2(0), texSize - ivec2(1));

    uint currentId = texelFetch(uObjectId, pixelCoord, 0).r;
    bool insideSelected = isSelectedObject(currentId);
    bool hasEdge = insideSelected || hasSelectedNeighbor(pixelCoord, texSize);

    if (!hasEdge)
        return;

    float mixFactor = insideSelected ? (pc.outlineMix * 0.35) : pc.outlineMix;
    outColor.rgb = mix(baseColor.rgb, pc.outlineColor.rgb, clamp(mixFactor, 0.0, 1.0));
}
