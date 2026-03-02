#version 450

const int MAX_BONE_INFLUENCE = 4;

layout(push_constant) uniform LightSpaceMatrixPushConstant
{
    mat4 lightSpaceMatrix;
    uint baseInstance;
} lightSpaceMatrixPushConstant;

layout(std430, set = 0, binding = 0) readonly buffer BonesSSBO
{
    mat4 boneMatrices[];
} bonesData;

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
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in ivec4 inBoneIds;
layout(location = 6) in vec4 inWeights;

void main()
{
    uint instanceIndex = lightSpaceMatrixPushConstant.baseInstance + uint(gl_InstanceIndex);
    InstanceData instanceData = instanceDataBuffer.instances[instanceIndex];
    mat4 modelMatrix = instanceData.model;
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

    gl_Position = lightSpaceMatrixPushConstant.lightSpaceMatrix *
                  modelMatrix *
                  boneTransform *
                  vec4(inPosition, 1.0);
}
