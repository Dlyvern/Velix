#version 450

layout(push_constant) uniform GlassPC
{
    uint  baseInstance;
    float ior;
    float frosted;
    float tintR;
    float tintG;
    float tintB;
    float _pad0;
    float _pad1;
} pc;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

struct InstanceData
{
    mat4  model;
    uvec4 objectInfo; // x = objectId
};

layout(std430, set = 2, binding = 1) readonly buffer InstanceDataSSBO
{
    InstanceData instances[];
} instanceDataBuffer;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTextures;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec3 inTangent;

layout(location = 0) out vec2  vUV;
layout(location = 1) out vec3  vNormalView;
layout(location = 2) out vec3  vViewPos;
layout(location = 3) out vec4  vClipPos;   // for screen-UV derivation in fragment

void main()
{
    uint         idx      = pc.baseInstance + uint(gl_InstanceIndex);
    InstanceData inst     = instanceDataBuffer.instances[idx];
    mat4         model    = inst.model;

    mat3 normalMat  = transpose(inverse(mat3(model)));
    vec3 worldNorm  = normalize(normalMat * inNormal);
    vNormalView     = normalize(mat3(camera.view) * worldNorm);

    vec4 worldPos = model * vec4(inPosition, 1.0);
    vec4 viewPos  = camera.view * worldPos;
    vViewPos      = viewPos.xyz;
    vUV           = inTextures;

    vec4 clip = camera.projection * viewPos;
    vClipPos  = clip;
    gl_Position = clip;
}
