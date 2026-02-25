#version 450

const int MAX_BONE_INFLUENCE = 4;

layout(push_constant) uniform LightSpaceMatrixPushConstant
{
    mat4 lightSpaceMatrix;
    mat4 model;
    uint bonesOffset;
} lightSpaceMatrixPushConstant;

layout(std430, set = 0, binding = 0) readonly buffer BonesSSBO
{
    mat4 boneMatrices[];
} bonesData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTextures;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in ivec4 inBoneIds;
layout(location = 6) in vec4 inWeights;

void main()
{
    mat4 boneTransform = mat4(0.0);
    bool hasBone = false;

    for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
    {
        int id = inBoneIds[i];
        if (id >= 0)
        {
            boneTransform += bonesData.boneMatrices[lightSpaceMatrixPushConstant.bonesOffset + uint(id)] * inWeights[i];
            hasBone = true;
        }
    }

    if (!hasBone)
        boneTransform = mat4(1.0);

    gl_Position = lightSpaceMatrixPushConstant.lightSpaceMatrix *
                  lightSpaceMatrixPushConstant.model *
                  boneTransform *
                  vec4(inPosition, 1.0);
}
