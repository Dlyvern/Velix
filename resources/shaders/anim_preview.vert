#version 450

const int MAX_BONE_INFLUENCE = 4;

layout(push_constant) uniform PC
{
    mat4 model;
} pc;

layout(set = 0, binding = 0) uniform PreviewCameraUBO
{
    mat4 view;
    mat4 proj;
} camera;

layout(std430, set = 0, binding = 1) readonly buffer BoneSSBO
{
    mat4 bones[];
} boneData;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in ivec4 inBoneIds;
layout(location = 6) in vec4 inWeights;

layout(location = 0) out vec2 fragTextureCoordinates;

void main()
{
    mat4 boneTransform = mat4(0.0);
    bool hasBone = false;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int id = inBoneIds[i];
        if (id >= 0)
        {
            boneTransform += boneData.bones[id] * inWeights[i];
            hasBone = true;
        }
    }

    if (!hasBone)
        boneTransform = mat4(1.0);

    fragTextureCoordinates = inTexCoord;

    gl_Position = camera.proj * camera.view * pc.model * boneTransform * vec4(inPosition, 1.0);
}
