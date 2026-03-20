#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormalView;
layout(location = 2) in vec3 fragPositionView;
layout(location = 3) in vec3 fragTangentView;
layout(location = 4) in vec3 fragBitangentView;
layout(location = 5) in flat uint fragObjectId;
layout(location = 6) in flat uint fragMaterialIndex;

layout(location = 0) out vec4 outGBufferNormal;   // normal (encoded)
layout(location = 1) out vec4 outGBufferAlbedo;   // albedo + alpha
layout(location = 2) out vec4 outGBufferMaterial; // ao, roughness, metallic, reserved
layout(location = 3) out vec4 outGBufferEmissive; // emissive rgb
layout(location = 4) out uint outObjectId;

layout(set = 0, binding = 0) uniform CameraUniformObject
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} cameraUniformObject;

// Bindless texture array — all engine textures are registered here.
layout(set = 1, binding = 0) uniform sampler2D allTextures[];

// Material params SSBO — one entry per registered material.
struct MaterialGPUParams
{
    vec4  baseColorFactor;
    vec4  emissiveFactor;
    vec4  uvTransform;   // xy = scale, zw = offset
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float aoStrength;
    uint  flags;
    float alphaCutoff;
    float uvRotation;    // degrees
    float ior;
    uint  albedoTexIdx;
    uint  normalTexIdx;
    uint  ormTexIdx;
    uint  emissiveTexIdx;
};

layout(set = 1, binding = 1, std430) readonly buffer MaterialBuffer
{
    MaterialGPUParams materials[];
} materialBuffer;

layout(push_constant) uniform GBufferPC
{
    uint  baseInstance;
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
    float time; // seconds since engine start
} pc;

const uint MATERIAL_FLAG_ALPHA_MASK = 1u << 0;
const uint MATERIAL_FLAG_FLIP_V     = 1u << 4;
const uint MATERIAL_FLAG_FLIP_U     = 1u << 5;
const uint MATERIAL_FLAG_CLAMP_UV   = 1u << 6;

vec2 getUV(MaterialGPUParams mat)
{
    vec2 uv = fragUV;
    if ((mat.flags & MATERIAL_FLAG_FLIP_U) != 0u)
        uv.x = 1.0 - uv.x;
    if ((mat.flags & MATERIAL_FLAG_FLIP_V) != 0u)
        uv.y = 1.0 - uv.y;

    uv *= mat.uvTransform.xy;
    float rotRad = radians(mat.uvRotation);
    float c = cos(rotRad);
    float s = sin(rotRad);
    uv = (mat2(c, -s, s, c) * uv) + mat.uvTransform.zw;

    if ((mat.flags & MATERIAL_FLAG_CLAMP_UV) != 0u)
        uv = clamp(uv, vec2(0.0), vec2(1.0));

    return uv;
}

vec3 getNormalView(vec3 normalTS, MaterialGPUParams mat)
{
    vec3 N = normalize(fragNormalView);
    vec3 T = normalize(fragTangentView);
    vec3 B = normalize(fragBitangentView);

    T = normalize(T - dot(T, N) * N);
    B = normalize(cross(N, T));

    mat3 TBN = mat3(T, B, N);

    normalTS.xy *= mat.normalScale;
    normalTS     = normalize(normalTS);

    return normalize(TBN * normalTS);
}

void main()
{
    outObjectId = fragObjectId;

    MaterialGPUParams mat = materialBuffer.materials[fragMaterialIndex];

    vec2 uv = getUV(mat);

    vec4 albedoTex = texture(allTextures[nonuniformEXT(mat.albedoTexIdx)], uv);
    vec3 albedo    = albedoTex.rgb * mat.baseColorFactor.rgb;
    float alpha    = albedoTex.a  * mat.baseColorFactor.a;

    if ((mat.flags & MATERIAL_FLAG_ALPHA_MASK) != 0u)
    {
        if (alpha < mat.alphaCutoff)
            discard;
    }

    vec3 emissive = texture(allTextures[nonuniformEXT(mat.emissiveTexIdx)], uv).rgb * mat.emissiveFactor.rgb;

    vec3 normalSample = texture(allTextures[nonuniformEXT(mat.normalTexIdx)], uv).xyz * 2.0 - 1.0;

    vec3  orm       = texture(allTextures[nonuniformEXT(mat.ormTexIdx)], uv).rgb;
    float ao        = mix(1.0, orm.r, mat.aoStrength);
    float roughness = clamp(orm.g * mat.roughnessFactor, 0.04, 1.0);
    float metallic  = clamp(orm.b * mat.metallicFactor,  0.0,  1.0);

    {
        vec3 dnx = dFdxFine(normalSample);
        vec3 dny = dFdyFine(normalSample);
        float variance = dot(dnx, dnx) + dot(dny, dny);

        float dzx = dFdxFine(gl_FragCoord.z);
        float dzy = dFdyFine(gl_FragCoord.z);
        float edgeFactor = 1.0 - smoothstep(0.0001, 0.005, abs(dzx) + abs(dzy));

        float kernelRoughness = min(0.5 * variance * edgeFactor, 0.3);
        roughness = sqrt(clamp(roughness * roughness + kernelRoughness, 0.0, 1.0));
    }

    vec3 N    = getNormalView(normalSample, mat);
    vec3 encN = N * 0.5 + 0.5;

    outGBufferNormal   = vec4(encN, 1.0);
    outGBufferAlbedo   = vec4(albedo, alpha);
    outGBufferMaterial = vec4(ao, roughness, metallic, 0.0);
    outGBufferEmissive = vec4(emissive, 1.0);
}
