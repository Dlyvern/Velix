#version 450

layout(push_constant) uniform DepthPrepassPC
{
    uint  baseInstance;
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
    float time;
} pc;

layout(set = 0, binding = 0) uniform CameraUniformObject
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} cameraUniformObject;

struct InstanceData
{
    mat4 model;
    uvec4 objectInfo; // x = objectId, y = bonesOffset, z = materialIndex, w = reserved
};

layout(std430, set = 2, binding = 1) readonly buffer InstanceDataSSBO
{
    InstanceData instances[];
} instanceDataBuffer;

layout(location = 0) in vec3 inPosition;

void main()
{
    uint instanceIndex = pc.baseInstance + uint(gl_InstanceIndex);
    mat4 modelMatrix = instanceDataBuffer.instances[instanceIndex].model;
    vec4 viewPos = cameraUniformObject.view * modelMatrix * vec4(inPosition, 1.0);
    gl_Position = cameraUniformObject.projection * viewPos;
}
