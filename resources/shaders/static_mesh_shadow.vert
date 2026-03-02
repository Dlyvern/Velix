#version 450

layout(push_constant) uniform LightSpaceMatrixPushConstant
{
    mat4 lightSpaceMatrix;
    uint baseInstance;
} lightSpaceMatrixPushConstant;

struct InstanceData
{
    mat4 model;
    uvec4 objectInfo; // x = objectId, y = bonesOffset, z/w = reserved
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceDataSSBO
{
    InstanceData instances[];
} instanceDataBuffer;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTextures;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec3 inTangent;

void main()
{
    uint instanceIndex = lightSpaceMatrixPushConstant.baseInstance + uint(gl_InstanceIndex);
    mat4 modelMatrix = instanceDataBuffer.instances[instanceIndex].model;
    gl_Position = lightSpaceMatrixPushConstant.lightSpaceMatrix * modelMatrix * vec4(inPosition, 1.0);
}
