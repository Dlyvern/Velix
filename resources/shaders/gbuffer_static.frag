#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormalView;
layout(location = 2) in vec3 fragPositionView;
layout(location = 3) in vec3 fragTangentView;
layout(location = 4) in vec3 fragBitangentView;
layout(location = 5) in flat uint fragObjectId;

layout(location = 0) out vec4 outGBufferNormal;   // normal (encoded)
layout(location = 1) out vec4 outGBufferAlbedo;   // albedo + alpha
layout(location = 2) out vec4 outGBufferMaterial; // ao, roughness, metallic, emissiveStrength/flags
layout(location = 3) out uint outObjectId;
layout(location = 4) out vec4 outGBufferTangentAniso; // tangent (encoded), aniso mask

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
    float uvRotation; // degrees
    float ior;
} material;

vec2 getUV()
{
    vec2 uv = fragUV * material.uvTransform.xy;
    float rotationRadians = radians(material.uvRotation);
    float c = cos(rotationRadians);
    float s = sin(rotationRadians);
    mat2 rotation = mat2(c, -s, s, c);
    return (rotation * uv) + material.uvTransform.zw;
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

vec3 getTangentView()
{
    vec3 N = normalize(fragNormalView);
    vec3 T = normalize(fragTangentView);
    vec3 B = normalize(fragBitangentView);

    T = normalize(T - dot(T, N) * N);
    if (length(T) < 0.001)
        T = normalize(cross(abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0), N));

    // Preserve handedness from mesh tangent frame.
    float handedness = dot(cross(N, T), B) < 0.0 ? -1.0 : 1.0;
    vec3 fixedB = normalize(cross(N, T)) * handedness;
    T = normalize(cross(fixedB, N));

    return T;
}

void main()
{
    outObjectId = fragObjectId;

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

    // Specular AA (Tokuyoshi & Kaplanyan 2019): widen roughness based on
    // normal-map screen-space variance to suppress specular flickering.
    {
        vec3 normalRaw = texture(uNormalTex, uv).xyz * 2.0 - 1.0;
        vec3 dnx = dFdx(normalRaw);
        vec3 dny = dFdy(normalRaw);
        float variance = dot(dnx, dnx) + dot(dny, dny);
        float kernelRoughness = min(2.0 * variance, 1.0);
        roughness = sqrt(clamp(roughness * roughness + kernelRoughness, 0.0, 1.0));
    }

    vec3 N = getNormalView();

    vec3 encN = N * 0.5 + 0.5;

    outGBufferNormal = vec4(encN, 1.0);
    outGBufferAlbedo = vec4(albedo, alpha);
    outGBufferMaterial = vec4(ao, roughness, metallic, emissiveStrength);
    outGBufferTangentAniso = vec4(getTangentView() * 0.5 + 0.5, 1.0);
}
