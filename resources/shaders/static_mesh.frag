#version 450

const int DIRECTIONAL_LIGHT_TYPE = 0;
const int SPOT_LIGHT_TYPE = 1;
const int POINT_LIGHT_TYPE = 2;

const int MAX_LIGHT_COUNT = 16;
const float PI = 3.14159265359;

struct Light
{
    vec4 position;        // xyz in view space
    vec4 direction;       // xyz in view space
    vec4 colorStrength;   // rgb=color, a=intensity
    vec4 parameters;      // x=innerCutoff, y=outerCutoff, z=radius, w=lightType
};

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 fragNormalView;
layout(location = 2) in vec3 fragPositionView;
layout(location = 3) in vec4 fragPositionLightSpace;
layout(location = 4) in vec3 fragTangentView;
layout(location = 5) in vec3 fragBitangentView;

layout(location = 0) out vec4 outColor;
layout(location = 1) out uint outObjectId;

layout(push_constant) uniform ObjectIdPushConstant
{
    mat4 model;
    uint objectId;
} objectIdPushConstant;

layout(set = 0, binding = 2) uniform sampler2D shadowMap;

layout(std430, set = 0, binding = 3) readonly buffer LightSSBO
{
    int lightCount;
    Light lights[];
} lightData;

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

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / max(denom, 0.000001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; 

    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.000001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float calculateDirectionalLightShadow(vec3 lightDirection, vec3 normal, sampler2D shadowMapToUse)
{
    vec3 projCoords = fragPositionLightSpace.xyz / fragPositionLightSpace.w;
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (texCoords.x < 0.0 || texCoords.x > 1.0 ||
        texCoords.y < 0.0 || texCoords.y > 1.0 || currentDepth > 1.0)
        return 0.0;

    float cosNL = max(dot(normal, lightDirection), 0.0);
    float bias = max(0.0006 * (1.0 - cosNL), 0.00005);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMapToUse, 0));

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMapToUse, texCoords + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

vec3 getLightRadianceAndDirection(in Light light, in vec3 fragPosView, out vec3 L, out float shadowFactor)
{
    shadowFactor = 0.0;

    int lightType = int(light.parameters.w);
    vec3 color = light.colorStrength.rgb * light.colorStrength.a;

    if (lightType == DIRECTIONAL_LIGHT_TYPE)
    {
        L = normalize(-light.direction.xyz);
        shadowFactor = calculateDirectionalLightShadow(L, normalize(fragNormalView), shadowMap);
        return color;
    }
    else if (lightType == POINT_LIGHT_TYPE)
    {
        vec3 toLight = light.position.xyz - fragPosView;
        float distance = length(toLight);
        L = (distance > 0.0) ? toLight / distance : vec3(0.0, 0.0, 1.0);

        float radius = max(light.parameters.z, 0.0001);
        float attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
        attenuation *= attenuation;

        return color * attenuation;
    }
    else if (lightType == SPOT_LIGHT_TYPE)
    {
        vec3 toLight = light.position.xyz - fragPosView;
        float distance = length(toLight);
        L = (distance > 0.0) ? toLight / distance : vec3(0.0, 0.0, 1.0);

        float radius = max(light.parameters.z, 0.0001);
        float attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
        attenuation *= attenuation;

        float theta = dot(L, normalize(-light.direction.xyz));
        float innerCutoff = light.parameters.x;
        float outerCutoff = light.parameters.y;
        float epsilon = max(innerCutoff - outerCutoff, 0.0001);
        float spot = clamp((theta - outerCutoff) / epsilon, 0.0, 1.0);

        return color * attenuation * spot;
    }

    L = vec3(0.0, 0.0, 1.0);
    return vec3(0.0);
}

void main()
{
    outObjectId = objectIdPushConstant.objectId;

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

    vec3 orm = texture(uOrmTex, uv).rgb;
    float ao = mix(1.0, orm.r, material.aoStrength);
    float roughness = clamp(orm.g * material.roughnessFactor, 0.04, 1.0);
    float metallic = clamp(orm.b * material.metallicFactor, 0.0, 1.0);

    vec3 N = getNormalView();
    vec3 V = normalize(-fragPositionView);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);

    int count = min(lightData.lightCount, MAX_LIGHT_COUNT);

    for (int i = 0; i < count; ++i)
    {
        Light light = lightData.lights[i];

        vec3 L;
        float shadow;
        vec3 radiance = getLightRadianceAndDirection(light, fragPositionView, L, shadow);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0)
            continue;

        vec3 H = normalize(V + L);

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3 specular = numerator / denom;

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= (1.0 - metallic); // metals have no diffuse

        vec3 diffuse = (kD * albedo) / PI;

        vec3 lightContrib = (diffuse + specular) * radiance * NdotL;

        lightContrib *= (1.0 - shadow);

        Lo += lightContrib;
    }

    vec3 ambient = albedo * 0.03 * ao;

    vec3 color = ambient + Lo + emissive;

    outColor = vec4(color, alpha);
}