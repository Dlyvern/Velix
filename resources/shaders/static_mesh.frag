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
layout(set = 0, binding = 0) uniform CameraUniformObject
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} cameraUniformObject;

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
    float uvRotation; // degrees
    float ior;
} material;

const uint MATERIAL_FLAG_ALPHA_MASK = 1u << 0;
const uint MATERIAL_FLAG_FLIP_V = 1u << 4;
const uint MATERIAL_FLAG_FLIP_U = 1u << 5;
const uint MATERIAL_FLAG_CLAMP_UV = 1u << 6;

vec2 getUV()
{
    vec2 uv = fragUV;
    if ((material.flags & MATERIAL_FLAG_FLIP_U) != 0u)
        uv.x = 1.0 - uv.x;
    if ((material.flags & MATERIAL_FLAG_FLIP_V) != 0u)
        uv.y = 1.0 - uv.y;

    uv *= material.uvTransform.xy;
    float rotationRadians = radians(material.uvRotation);
    float c = cos(rotationRadians);
    float s = sin(rotationRadians);
    mat2 rotation = mat2(c, -s, s, c);
    uv = (rotation * uv) + material.uvTransform.zw;

    if ((material.flags & MATERIAL_FLAG_CLAMP_UV) != 0u)
        uv = clamp(uv, vec2(0.0), vec2(1.0));

    return uv;
}

vec3 getNormalView(vec2 uv)
{
    vec3 N = normalize(fragNormalView);
    vec3 T = normalize(fragTangentView);
    vec3 B = normalize(fragBitangentView);

    T = normalize(T - dot(T, N) * N);
    B = normalize(cross(N, T));

    mat3 TBN = mat3(T, B, N);

    vec3 normalTS = texture(uNormalTex, uv).xyz * 2.0 - 1.0;
    normalTS.xy *= material.normalScale;
    normalTS = normalize(normalTS);

    return normalize(TBN * normalTS);
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

    if (lightType == POINT_LIGHT_TYPE)
    {
        vec3 toLight = light.position.xyz - fragPosView;
        float distance = length(toLight);
        L = (distance > 0.0) ? toLight / distance : vec3(0.0, 0.0, 1.0);

        float radius = max(light.parameters.z, 0.0001);
        float attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
        attenuation *= attenuation;

        return color * attenuation;
    }

    if (lightType == SPOT_LIGHT_TYPE)
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

float D_GGX(float NdotH, float a2)
{
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return G_SchlickGGX(max(NdotV, 0.001), k) * G_SchlickGGX(max(NdotL, 0.001), k);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

vec3 evaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
        return vec3(0.0);

    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float a = roughness * roughness;
    float a2 = a * a;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(HdotV, F0);
    float D = D_GGX(NdotH, a2);
    float G = G_Smith(NdotV, NdotL, roughness);

    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    return (diffuse + specular) * NdotL;
}

void main()
{
    outObjectId = objectIdPushConstant.objectId;

    vec2 uv = getUV();

    vec4 albedoTex = texture(uAlbedoTex, uv);
    vec3 albedo = albedoTex.rgb * material.baseColorFactor.rgb;
    float alpha = albedoTex.a * material.baseColorFactor.a;

    if ((material.flags & MATERIAL_FLAG_ALPHA_MASK) != 0u && alpha < material.alphaCutoff)
        discard;

    vec3 emissive = texture(uEmissiveTex, uv).rgb * material.emissiveFactor.rgb;

    vec3 orm = texture(uOrmTex, uv).rgb;
    float ao = mix(1.0, orm.r, material.aoStrength);
    float roughness = clamp(orm.g * material.roughnessFactor, 0.04, 1.0);
    float metallic = clamp(orm.b * material.metallicFactor, 0.0, 1.0);

    vec3 N = getNormalView(uv);
    vec3 V = normalize(-fragPositionView);
    vec3 lighting = vec3(0.0);
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

        lighting += evaluateBRDF(N, V, L, albedo, metallic, roughness) * radiance * (1.0 - shadow);
    }

    vec3 ambient = albedo * 0.03 * ao;
    vec3 color = ambient + lighting + emissive;

    outColor = vec4(color, alpha);
}
