#version 450

const int DIRECTIONAL_LIGHT_TYPE = 0;
const int SPOT_LIGHT_TYPE = 1;
const int POINT_LIGHT_TYPE = 2;
const int MAX_LIGHT_COUNT = 16;
const int MAX_DIRECTIONAL_CASCADES = 4;
const int MAX_SPOT_SHADOWS = 3;
const int MAX_POINT_SHADOWS = 1;

struct Light
{
    vec4 position;      // xyz in world space (bake uses world-space)
    vec4 direction;     // xyz in world space
    vec4 colorStrength; // rgb=color, a=intensity
    vec4 parameters;    // x=inner, y=outer, z=radius, w=type (unused w)
    vec4 shadowInfo;    // x=castsShadow, y=shadowIndex, z=far/range, w=near
};

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 0) out vec4 outIrradiance;

// Set 0 — reuse the engine camera descriptor set (camera UBO not used here).
layout(set = 0, binding = 1) uniform LightSpaceUBO
{
    mat4 lightSpaceMatrix;
    mat4 directionalLightSpaceMatrices[MAX_DIRECTIONAL_CASCADES];
    vec4 directionalCascadeSplits;
    mat4 spotLightSpaceMatrices[MAX_SPOT_SHADOWS];
} lightSpaceData;

layout(std430, set = 0, binding = 2) readonly buffer LightSSBO
{
    int lightCount;
    Light lights[];
} lightData;

// Set 1 — shadow maps
layout(set = 1, binding = 0) uniform sampler2DArray directionalShadowMaps;
layout(set = 1, binding = 1) uniform sampler2DArray spotShadowMaps;
layout(set = 1, binding = 2) uniform samplerCubeArray cubeShadowMaps;

layout(push_constant) uniform BakePC
{
    mat4 modelMatrix;
    mat4 normalMatrix;
} pc;

// Simple 3x3 PCF for directional shadow maps (world-space lookup)
float sampleDirectionalShadow(int cascadeIdx, vec3 worldPos, vec3 N, vec3 L)
{
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);
    vec4 posLS = lightSpaceData.directionalLightSpaceMatrices[cascadeIdx] * vec4(worldPos, 1.0);
    vec3 proj = posLS.xyz / posLS.w;
    proj.xy    = proj.xy * 0.5 + 0.5;

    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0)
        return 0.0; // Outside shadow map — no shadow

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(directionalShadowMaps, 0).xy);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(directionalShadowMaps,
                                     vec3(proj.xy + vec2(x, y) * texelSize, float(cascadeIdx))).r;
            shadow += (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

float sampleSpotShadow(int shadowIdx, vec3 worldPos, vec3 N, vec3 L)
{
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);
    vec4 posLS  = lightSpaceData.spotLightSpaceMatrices[shadowIdx] * vec4(worldPos, 1.0);
    vec3 proj   = posLS.xyz / posLS.w;
    proj.xy     = proj.xy * 0.5 + 0.5;

    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0)
        return 0.0;

    float pcfDepth = texture(spotShadowMaps, vec3(proj.xy, float(shadowIdx))).r;
    return (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
}

void main()
{
    vec3 N = normalize(inWorldNormal);
    vec3 irradiance = vec3(0.0);

    int count = min(lightData.lightCount, MAX_LIGHT_COUNT);
    for (int i = 0; i < count; ++i)
    {
        Light light  = lightData.lights[i];
        int lightType = int(light.parameters.w);
        vec3 radiance = light.colorStrength.rgb * light.colorStrength.a;
        bool castsShadow = light.shadowInfo.x > 0.5;
        int shadowIndex  = int(light.shadowInfo.y);

        vec3 L;
        float attenuation = 1.0;

        if (lightType == DIRECTIONAL_LIGHT_TYPE)
        {
            L = normalize(-light.direction.xyz);
        }
        else if (lightType == POINT_LIGHT_TYPE)
        {
            vec3 toLight = light.position.xyz - inWorldPos;
            float dist   = length(toLight);
            L = (dist > 0.0001) ? toLight / dist : vec3(0, 0, 1);
            float radius = max(light.parameters.z, 0.0001);
            attenuation  = clamp(1.0 - dist / radius, 0.0, 1.0);
            attenuation *= attenuation;
        }
        else if (lightType == SPOT_LIGHT_TYPE)
        {
            vec3 toLight = light.position.xyz - inWorldPos;
            float dist   = length(toLight);
            L = (dist > 0.0001) ? toLight / dist : vec3(0, 0, 1);
            float radius = max(light.parameters.z, 0.0001);
            attenuation  = clamp(1.0 - dist / radius, 0.0, 1.0);
            attenuation *= attenuation;
            float theta      = dot(L, normalize(-light.direction.xyz));
            float innerCut   = light.parameters.x;
            float outerCut   = light.parameters.y;
            float eps        = max(innerCut - outerCut, 0.0001);
            float spot       = clamp((theta - outerCut) / eps, 0.0, 1.0);
            attenuation     *= spot;
        }
        else
            continue;

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0)
            continue;

        float shadow = 0.0;
        if (castsShadow)
        {
            if (lightType == DIRECTIONAL_LIGHT_TYPE)
                shadow = sampleDirectionalShadow(shadowIndex, inWorldPos, N, L);
            else if (lightType == SPOT_LIGHT_TYPE)
                shadow = sampleSpotShadow(shadowIndex, inWorldPos, N, L);
            // Point light shadows (cube maps) skipped for simplicity.
        }

        irradiance += radiance * NdotL * attenuation * (1.0 - shadow);
    }

    outIrradiance = vec4(irradiance, 1.0);
}
