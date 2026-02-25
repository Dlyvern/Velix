#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormalView;
layout(location = 2) in vec3 fragPositionView;
layout(location = 3) in vec3 fragTangentView;
layout(location = 4) in vec3 fragBitangentView;

layout(location = 0) out vec4 outGBufferNormal;   // normal (encoded)
layout(location = 1) out vec4 outGBufferAlbedo;   // albedo + alpha
layout(location = 2) out vec4 outGBufferMaterial; // ao, roughness, metallic, emissiveStrength/flags
layout(location = 3) out uint outObjectId;

layout(push_constant) uniform ModelPushConstant
{
    mat4 model;
    uint objectId;
} modelPushConstant;

layout(set = 1, binding = 0) uniform sampler2D uAlbedoTex;
layout(set = 1, binding = 1) uniform sampler2D uNormalTex;
layout(set = 1, binding = 2) uniform sampler2D uOrmTex;      // R=AO G=Roughness B=Metallic
layout(set = 1, binding = 3) uniform sampler2D uEmissiveTex;

layout(set = 1, binding = 4) uniform MaterialParams
{
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    vec4 uvTransform; // xy scale, zw offset

    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float aoStrength;

    uint flags;
    float alphaCutoff;
    vec2 _padding;
} material;

vec2 getUV()
{
    return fragUV * material.uvTransform.xy + material.uvTransform.zw;
}

vec3 getNormalView()
{
    vec3 N = normalize(fragNormalView);
    vec3 T = normalize(fragTangentView);
    vec3 B = normalize(fragBitangentView);

    T = normalize(T - dot(T, N) * N);
    B = normalize(cross(N, T));

    mat3 TBN = mat3(T, B, N);

    vec3 normalTS = texture(uNormalTex, getUV()).xyz * 2.0 - 1.0;
    normalTS.xy *= material.normalScale;
    normalTS = normalize(normalTS);

    return normalize(TBN * normalTS);
}

void main()
{
    outObjectId = modelPushConstant.objectId;

    vec2 uv = getUV();

    vec4 albedoTex = texture(uAlbedoTex, uv);
    vec3 albedo = albedoTex.rgb * material.baseColorFactor.rgb;
    float alpha = albedoTex.a * material.baseColorFactor.a;

    if ((material.flags & 1u) != 0u)
    {
        if (alpha < material.alphaCutoff)
            discard;
    }

    vec3 emissive = texture(uEmissiveTex, uv).rgb * material.emissiveFactor.rgb;
    float emissiveStrength = max(max(emissive.r, emissive.g), emissive.b);

    vec3 orm = texture(uOrmTex, uv).rgb;
    float ao        = mix(1.0, orm.r, material.aoStrength);
    float roughness = clamp(orm.g * material.roughnessFactor, 0.04, 1.0);
    float metallic  = clamp(orm.b * material.metallicFactor, 0.0, 1.0);

    vec3 N = getNormalView();

    vec3 encN = N * 0.5 + 0.5;

    outGBufferNormal = vec4(encN, 1.0);
    outGBufferAlbedo = vec4(albedo, alpha);
    outGBufferMaterial = vec4(ao, roughness, metallic, emissiveStrength);
}