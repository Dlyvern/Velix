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
    vec4 position;
    vec4 direction;
    vec4 colorStrength;
    vec4 parameters;
    vec4 shadowInfo;
};

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 0) out vec4 outIrradiance;


layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

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


layout(set = 1, binding = 0) uniform sampler2DArray directionalShadowMaps;
layout(set = 1, binding = 1) uniform sampler2DArray spotShadowMaps;
layout(set = 1, binding = 2) uniform samplerCubeArray cubeShadowMaps;


float sampleDirectionalShadowCascade(int cascadeIdx, vec3 worldPos, float bias)
{
    vec4 posLS = lightSpaceData.directionalLightSpaceMatrices[cascadeIdx] * vec4(worldPos, 1.0);
    vec3 proj = posLS.xyz / posLS.w;
    proj.xy   = proj.xy * 0.5 + 0.5;

    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0)
        return -1.0;

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(directionalShadowMaps, 0).xy);
    float kernelScale = 1.0 + float(cascadeIdx) * 0.75;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(directionalShadowMaps,
                                     vec3(proj.xy + vec2(x, y) * texelSize * kernelScale, float(cascadeIdx))).r;
            shadow += (proj.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}



int selectCascade(float viewDepth)
{
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.x)
        return 0;
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.y)
        return 1;
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.z)
        return 2;
    return 3;
}

float sampleDirectionalShadow(int lightIdx, vec3 worldPos, vec3 N, vec3 L)
{
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);


    float viewDepth = -(camera.view * vec4(worldPos, 1.0)).z;
    int cascade = selectCascade(max(viewDepth, 0.0));

    float s = sampleDirectionalShadowCascade(cascade, worldPos, bias);
    if (s >= 0.0)
        return s;


    for (int c = 0; c < MAX_DIRECTIONAL_CASCADES; ++c)
    {
        if (c == cascade) continue;
        s = sampleDirectionalShadowCascade(c, worldPos, bias);
        if (s >= 0.0)
            return s;
    }
    return 0.0;
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


        vec3 lightDirWorld = normalize(mat3(camera.invView) * light.direction.xyz);
        vec3 lightPosWorld = (camera.invView * vec4(light.position.xyz, 1.0)).xyz;

        vec3 L;
        float attenuation = 1.0;

        if (lightType == DIRECTIONAL_LIGHT_TYPE)
        {
            L = normalize(-lightDirWorld);
        }
        else if (lightType == POINT_LIGHT_TYPE)
        {
            vec3 toLight = lightPosWorld - inWorldPos;
            float dist   = length(toLight);
            L = (dist > 0.0001) ? toLight / dist : vec3(0, 0, 1);
            float radius = max(light.parameters.z, 0.0001);
            attenuation  = clamp(1.0 - dist / radius, 0.0, 1.0);
            attenuation *= attenuation;
        }
        else if (lightType == SPOT_LIGHT_TYPE)
        {
            vec3 toLight = lightPosWorld - inWorldPos;
            float dist   = length(toLight);
            L = (dist > 0.0001) ? toLight / dist : vec3(0, 0, 1);
            float radius = max(light.parameters.z, 0.0001);
            attenuation  = clamp(1.0 - dist / radius, 0.0, 1.0);
            attenuation *= attenuation;
            float theta      = dot(L, normalize(-lightDirWorld));
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
                shadow = sampleDirectionalShadow(i, inWorldPos, N, L);
            else if (lightType == SPOT_LIGHT_TYPE)
                shadow = sampleSpotShadow(shadowIndex, inWorldPos, N, L);

        }

        irradiance += radiance * NdotL * attenuation * (1.0 - shadow);
    }

    outIrradiance = vec4(irradiance, 1.0);
}
