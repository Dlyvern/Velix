#version 450

const int MAX_BONE_INFLUENCE = 4;




layout(push_constant) uniform ModelPushConstant
{
    uint  baseInstance;
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
    float time;
} modelPushConstant;

layout(set = 0, binding = 0) uniform CameraUniformObject
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
    mat4 prevView;
    mat4 prevProjection;
    vec4 jitter;
} cameraUniformObject;

layout(std430, set = 2, binding = 0) readonly buffer BonesSSBO
{
    mat4 boneMatrices[];
} bonesData;

struct InstanceData
{
    mat4 model;
    uvec4 objectInfo;
    mat4 prevModel;
};

layout(std430, set = 2, binding = 1) readonly buffer InstanceDataSSBO
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

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormalView;
layout(location = 2) out vec3 fragPositionView;
layout(location = 3) out vec3 fragTangentView;
layout(location = 4) out vec3 fragBitangentView;
layout(location = 5) out flat uint fragObjectId;
layout(location = 6) out flat uint fragMaterialIndex;
layout(location = 7) out vec2 fragLightmapUV;
layout(location = 8) out flat uint fragLightmapTexIdx;
layout(location = 9)  out vec4 fragCurrClipPos;
layout(location = 10) out vec4 fragPrevClipPos;

void main()
{
    uint instanceIndex = modelPushConstant.baseInstance + uint(gl_InstanceIndex);
    InstanceData instanceData = instanceDataBuffer.instances[instanceIndex];
    uint bonesOffset = instanceData.objectInfo.y;
    mat4 modelMatrix = instanceData.model;
    fragObjectId      = instanceData.objectInfo.x;
    fragMaterialIndex = instanceData.objectInfo.z;

    mat4 boneTransform = mat4(0.0);

    bool hasBone = false;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
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

    fragUV = inTextures;

    vec4 worldPos = modelMatrix * boneTransform * vec4(inPosition, 1.0);
    vec4 viewPos = cameraUniformObject.view * worldPos;
    mat3 normalMatrix = transpose(inverse(mat3(modelMatrix * boneTransform)));
    vec3 worldNormal = normalize(normalMatrix * inNormal);
    vec3 worldTangent = normalize(normalMatrix * inTangent);
    vec3 worldBitangent = normalize(normalMatrix * inBitangent);

    fragPositionView = viewPos.xyz;
    mat3 view3 = mat3(cameraUniformObject.view);
    fragNormalView = normalize(view3 * worldNormal);
    fragTangentView = normalize(view3 * worldTangent);
    fragBitangentView = normalize(view3 * worldBitangent);


    fragLightmapUV = vec2(0.0);
    fragLightmapTexIdx = 0xFFFFFFFFu;

    vec4 clipPos = cameraUniformObject.projection * viewPos;





    fragCurrClipPos = clipPos;
    vec4 prevWorldPos = instanceData.prevModel * boneTransform * vec4(inPosition, 1.0);
    fragPrevClipPos  = cameraUniformObject.prevProjection * cameraUniformObject.prevView * prevWorldPos;


    clipPos.xy += cameraUniformObject.jitter.xy * clipPos.w;

    gl_Position = clipPos;
}
