#version 450

// Binding 0 — main vertex data (Vertex3D layout)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords; // unused in bake
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inBitangent; // unused
layout(location = 4) in vec3 inTangent;   // unused
// Binding 1 — lightmap UV1
layout(location = 5) in vec2 inLightmapUV;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;

layout(push_constant) uniform BakePC
{
    mat4 modelMatrix;
    mat4 normalMatrix; // transpose(inverse(modelMatrix)), as mat4 for alignment
} pc;

void main()
{
    // Render the mesh in lightmap UV space: UV [0,1] → clip [-1,1].
    vec2 clipPos = inLightmapUV * 2.0 - 1.0;
    // Flip Y so UV (0,0) = top-left in screen space (matches Vulkan).
    gl_Position = vec4(clipPos.x, -clipPos.y, 0.0, 1.0);

    outWorldPos    = (pc.modelMatrix * vec4(inPosition, 1.0)).xyz;
    outWorldNormal = normalize(mat3(pc.normalMatrix) * inNormal);
}
