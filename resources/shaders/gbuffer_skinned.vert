#version 450

const int MAX_BONE_INFLUENCE = 4;

layout(push_constant) uniform ModelPushConstant
{
    mat4 model;
    uint objectId;
    uint bonesOffset;
} modelPushConstant;

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

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTextures;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in ivec4 inBoneIds;
layout(location = 6) in vec4 inWeights;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormalView;
layout(location = 2) out vec3 fragPositionView;
layout(location = 3) out vec3 fragTangentView;
layout(location = 4) out vec3 fragBitangentView;

void main()
{
    mat4 boneTransform = mat4(0.0);

    bool hasBone = false;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int id = inBoneIds[i];
        
        if (id >= 0)
        {
            boneTransform += bonesData.boneMatrices[modelPushConstant.bonesOffset + uint(id)] * inWeights[i];
            hasBone = true;
        }
    }

    if (!hasBone)
        boneTransform = mat4(1.0);

    fragUV = inTextures;

    vec4 worldPos = modelPushConstant.model * boneTransform * vec4(inPosition, 1.0);
    vec4 viewPos = cameraUniformObject.view * worldPos;
    mat3 normalMatrix = transpose(inverse(mat3(modelPushConstant.model * boneTransform)));
    vec3 worldNormal = normalize(normalMatrix * inNormal);
    vec3 worldTangent = normalize(normalMatrix * inTangent);
    vec3 worldBitangent = normalize(normalMatrix * inBitangent);

    fragPositionView = viewPos.xyz;
    mat3 view3 = mat3(cameraUniformObject.view);
    fragNormalView = normalize(view3 * worldNormal);
    fragTangentView = normalize(view3 * worldTangent);
    fragBitangentView = normalize(view3 * worldBitangent);

    gl_Position = cameraUniformObject.projection * viewPos;
}
