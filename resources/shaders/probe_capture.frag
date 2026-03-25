#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2  fragUV;
layout(location = 1) in vec3  fragNormalView;
layout(location = 2) in vec3  fragPositionView;
layout(location = 3) in vec3  fragTangentView;
layout(location = 4) in vec3  fragBitangentView;
layout(location = 5) in flat uint fragObjectId;
layout(location = 6) in flat uint fragMaterialIndex;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D allTextures[];

struct MaterialGPUParams
{
    vec4  baseColorFactor;
    vec4  emissiveFactor;
    vec4  uvTransform;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float aoStrength;
    uint  flags;
    float alphaCutoff;
    float uvRotation;
    float ior;
    uint  albedoTexIdx;
    uint  normalTexIdx;
    uint  ormTexIdx;
    uint  emissiveTexIdx;
};

layout(std430, set = 1, binding = 1) readonly buffer MaterialBuffer
{
    MaterialGPUParams materials[];
} materialBuffer;

layout(push_constant) uniform ProbeCapturePC
{
    uint  baseInstance;
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
    float time;
} pc;

const uint MATERIAL_FLAG_FLIP_V   = 1u << 4;
const uint MATERIAL_FLAG_FLIP_U   = 1u << 5;

void main()
{
    MaterialGPUParams mat = materialBuffer.materials[fragMaterialIndex];

    vec2 uv = fragUV;
    if ((mat.flags & MATERIAL_FLAG_FLIP_U) != 0u) uv.x = 1.0 - uv.x;
    if ((mat.flags & MATERIAL_FLAG_FLIP_V) != 0u) uv.y = 1.0 - uv.y;
    uv *= mat.uvTransform.xy;
    float rotRad = radians(mat.uvRotation);
    float c = cos(rotRad), s = sin(rotRad);
    uv = mat2(c, -s, s, c) * uv + mat.uvTransform.zw;

    vec3 albedo = mat.baseColorFactor.rgb;
    if (mat.albedoTexIdx > 0u)
        albedo *= texture(allTextures[nonuniformEXT(mat.albedoTexIdx)], uv).rgb;

    vec3 emissive = mat.emissiveFactor.rgb;
    if (mat.emissiveTexIdx > 0u)
        emissive *= texture(allTextures[nonuniformEXT(mat.emissiveTexIdx)], uv).rgb;

    // Flat ambient — probe captures scene color without view-dependent shading
    // so all faces show consistent brightness for environment reflections.
    outColor = vec4(albedo + emissive, 1.0);
}
