#version 450

const int MAX_BONE_INFLUENCE = 4;

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

layout(std430, set = 2, binding = 0) readonly buffer BonesSSBO
{
    mat4 boneMatrices[];
} bonesData;

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
layout(location = 5) in ivec4 inBoneIds;
layout(location = 6) in vec4 inWeights;

void main()
{
    uint instanceIndex = pc.baseInstance + uint(gl_InstanceIndex);
    InstanceData instanceData = instanceDataBuffer.instances[instanceIndex];
    uint bonesOffset = instanceData.objectInfo.y;

    mat4 boneTransform = mat4(0.0);
    bool hasBone = false;

    for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
    {
        int id = inBoneIds[i];
        if (id >= 0)
        {
            boneTransform += bonesData.boneMatrices[bonesOffset + uint(id)] * inWeights[i];
            hasBone = true;
        }
    }

    if (!hasBone)
        boneTransform = mat4(1.0);

    vec4 viewPos = cameraUniformObject.view * instanceData.model * boneTransform * vec4(inPosition, 1.0);
    gl_Position = cameraUniformObject.projection * viewPos;
}
