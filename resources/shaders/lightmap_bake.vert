#version 450


layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec3 inTangent;

layout(location = 5) in vec2 inLightmapUV;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;

layout(push_constant) uniform BakePC
{
    mat4 modelMatrix;
    mat4 normalMatrix;
} pc;

void main()
{

    vec2 clipPos = inLightmapUV * 2.0 - 1.0;
    gl_Position = vec4(clipPos.x, clipPos.y, 0.0, 1.0);

    outWorldPos    = (pc.modelMatrix * vec4(inPosition, 1.0)).xyz;
    outWorldNormal = normalize(mat3(pc.normalMatrix) * inNormal);
}
