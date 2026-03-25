#version 450

const int DIRECTIONAL_LIGHT_TYPE = 0;
const int SPOT_LIGHT_TYPE = 1;
const int POINT_LIGHT_TYPE = 2;
const int MAX_LIGHT_COUNT = 16;
const int MAX_DIRECTIONAL_CASCADES = 4;
const int MAX_SPOT_SHADOWS = 3;
const float PI = 3.14159265359;

struct Light
{
    vec4 position;
    vec4 direction;
    vec4 colorStrength;
    vec4 parameters;
    vec4 shadowInfo;
};

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

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

layout(set = 1, binding = 0) uniform sampler2D uDepth;
layout(set = 1, binding = 1) uniform sampler2DArray directionalShadowMaps;
layout(set = 1, binding = 2) uniform sampler2DArray spotShadowMaps;
layout(set = 1, binding = 3) uniform samplerCubeArray pointShadowMaps;

layout(push_constant) uniform FogLightingPC
{
    vec4 fogColorDensity;
    vec4 fogParams0;
    vec4 fogParams1;
    vec4 fogParams2;
    vec4 extentInfo;
} pc;

vec3 reconstructViewPosition(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndc;
    return viewPos.xyz / max(viewPos.w, 0.000001);
}

float phaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = pow(max(1.0 + g2 - 2.0 * g * cosTheta, 0.001), 1.5);
    return (1.0 - g2) / (4.0 * PI * denom);
}

float hash13(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float n000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));

    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    return mix(nxy0, nxy1, f.z);
}

int selectDirectionalCascade(float viewDepth)
{
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.x)
        return 0;
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.y)
        return 1;
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.z)
        return 2;
    return 3;
}

float sampleDirectionalShadow(int cascadeIndex, vec3 worldPos)
{
    vec4 posLS = lightSpaceData.directionalLightSpaceMatrices[cascadeIndex] * vec4(worldPos, 1.0);
    vec3 projCoords = posLS.xyz / max(posLS.w, 0.0001);
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (texCoords.x < 0.0 || texCoords.x > 1.0 ||
        texCoords.y < 0.0 || texCoords.y > 1.0 ||
        currentDepth < 0.0 || currentDepth > 1.0)
        return 1.0;

    float bias = 0.0002 + float(cascadeIndex) * 0.0001;
    float visibility = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(directionalShadowMaps, 0).xy);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float shadowDepth = texture(directionalShadowMaps, vec3(texCoords + vec2(x, y) * texelSize, float(cascadeIndex))).r;
            visibility += (currentDepth - bias) <= shadowDepth ? 1.0 : 0.0;
        }
    }

    return visibility / 9.0;
}

float sampleDirectionalShadowBlended(float viewDepth, vec3 worldPos)
{
    float cascadeStart[4] = float[4](
        0.0,
        lightSpaceData.directionalCascadeSplits.x,
        lightSpaceData.directionalCascadeSplits.y,
        lightSpaceData.directionalCascadeSplits.z);
    float cascadeEnd[4] = float[4](
        lightSpaceData.directionalCascadeSplits.x,
        lightSpaceData.directionalCascadeSplits.y,
        lightSpaceData.directionalCascadeSplits.z,
        lightSpaceData.directionalCascadeSplits.z * 4.0);

    int cascadeIndex = selectDirectionalCascade(viewDepth);
    float visibility = sampleDirectionalShadow(cascadeIndex, worldPos);
    if (cascadeIndex < 3)
    {
        float rangeStart = cascadeStart[cascadeIndex];
        float rangeEnd = cascadeEnd[cascadeIndex];
        float blendStart = mix(rangeStart, rangeEnd, 0.8);
        if (viewDepth > blendStart)
        {
            float blend = clamp((viewDepth - blendStart) / max(rangeEnd - blendStart, 0.0001), 0.0, 1.0);
            float nextVisibility = sampleDirectionalShadow(cascadeIndex + 1, worldPos);
            visibility = mix(visibility, nextVisibility, smoothstep(0.0, 1.0, blend));
        }
    }

    return visibility;
}

float sampleSpotShadow(int shadowIndex, vec3 worldPos)
{
    if (shadowIndex < 0 || shadowIndex >= MAX_SPOT_SHADOWS)
        return 1.0;

    vec4 posLS = lightSpaceData.spotLightSpaceMatrices[shadowIndex] * vec4(worldPos, 1.0);
    vec3 projCoords = posLS.xyz / max(posLS.w, 0.0001);
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (texCoords.x < 0.0 || texCoords.x > 1.0 ||
        texCoords.y < 0.0 || texCoords.y > 1.0 ||
        currentDepth < 0.0 || currentDepth > 1.0)
        return 1.0;

    float bias = 0.00025;
    float visibility = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(spotShadowMaps, 0).xy);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float shadowDepth = texture(spotShadowMaps, vec3(texCoords + vec2(x, y) * texelSize, float(shadowIndex))).r;
            visibility += (currentDepth - bias) <= shadowDepth ? 1.0 : 0.0;
        }
    }

    return visibility / 9.0;
}

void main()
{
    float depth = texture(uDepth, vUV).r;
    vec3 farViewPos = reconstructViewPosition(vUV, 0.9995);
    vec3 surfaceViewPos = depth < 0.9999 ? reconstructViewPosition(vUV, depth) : farViewPos;

    float rayEndDistance = depth < 0.9999 ? length(surfaceViewPos) : (pc.fogParams2.z > 0.5 ? 90.0 : 60.0);
    float startDistance = max(pc.fogParams0.x, 0.0);
    if (rayEndDistance <= startDistance + 0.001)
    {
        outColor = vec4(0.0);
        return;
    }

    vec3 rayDirView = normalize(surfaceViewPos);
    vec3 viewDirToCamera = normalize(-rayDirView);
    int steps = pc.fogParams2.z > 0.5 ? 32 : 16;
    float stepSize = (rayEndDistance - startDistance) / float(steps);

    vec3 scattering = vec3(0.0);
    float transmittance = 1.0;

    int count = min(lightData.lightCount, MAX_LIGHT_COUNT);
    for (int i = 0; i < steps; ++i)
    {
        float rayDistance = startDistance + (float(i) + 0.5) * stepSize;
        vec3 sampleViewPos = rayDirView * rayDistance;
        vec3 sampleWorldPos = (camera.invView * vec4(sampleViewPos, 1.0)).xyz;

        float heightFactor = exp(-max(sampleWorldPos.y - pc.fogParams0.z, 0.0) * pc.fogParams0.w);
        vec3 noisePos = sampleWorldPos * pc.fogParams1.w + vec3(pc.fogParams2.y * pc.fogParams2.x, 0.0, pc.fogParams2.y * pc.fogParams2.x * 0.67);
        float dustNoise = mix(1.0, mix(0.65, 1.35, noise3(noisePos)), pc.fogParams1.z);
        float density = pc.fogColorDensity.w * heightFactor * dustNoise;
        if (density <= 0.00001)
            continue;

        vec3 localScatter = pc.fogColorDensity.rgb * density * 0.18;

        for (int lightIndex = 0; lightIndex < count; ++lightIndex)
        {
            Light light = lightData.lights[lightIndex];
            int lightType = int(light.parameters.w);
            vec3 radiance = light.colorStrength.rgb * light.colorStrength.a;
            vec3 L = vec3(0.0);
            float attenuation = 1.0;
            float visibility = 1.0;

            if (lightType == DIRECTIONAL_LIGHT_TYPE)
            {
                L = normalize(-light.direction.xyz);
                if (light.shadowInfo.x > 0.5)
                    visibility = sampleDirectionalShadowBlended(max(-sampleViewPos.z, 0.0), sampleWorldPos);
                attenuation = pc.fogParams1.y;
            }
            else if (lightType == SPOT_LIGHT_TYPE)
            {
                vec3 toLight = light.position.xyz - sampleViewPos;
                float distanceToLight = length(toLight);
                if (distanceToLight <= 0.0001)
                    continue;

                L = toLight / distanceToLight;
                float radius = max(light.parameters.z, 0.0001);
                attenuation = clamp(1.0 - (distanceToLight / radius), 0.0, 1.0);
                attenuation *= attenuation;

                float theta = dot(L, normalize(-light.direction.xyz));
                float innerCutoff = light.parameters.x;
                float outerCutoff = light.parameters.y;
                float epsilon = max(innerCutoff - outerCutoff, 0.0001);
                float coneFactor = clamp((theta - outerCutoff) / epsilon, 0.0, 1.0);
                attenuation *= coneFactor * pc.fogParams1.y;
                if (attenuation <= 0.0001)
                    continue;

                if (light.shadowInfo.x > 0.5)
                    visibility = sampleSpotShadow(int(light.shadowInfo.y), sampleWorldPos);
            }
            else if (lightType == POINT_LIGHT_TYPE)
            {
                if (pc.fogParams2.w < 0.5)
                    continue;

                vec3 toLight = light.position.xyz - sampleViewPos;
                float distanceToLight = length(toLight);
                if (distanceToLight <= 0.0001)
                    continue;

                L = toLight / distanceToLight;
                float radius = max(light.parameters.z, 0.0001);
                attenuation = clamp(1.0 - (distanceToLight / radius), 0.0, 1.0);
                attenuation *= attenuation;
                attenuation *= 0.45;
                if (attenuation <= 0.0001)
                    continue;
            }
            else
            {
                continue;
            }

            float phase = phaseHG(dot(viewDirToCamera, L), pc.fogParams1.x);
            localScatter += radiance * attenuation * visibility * phase * density;
        }

        scattering += localScatter * transmittance * stepSize;
        transmittance *= exp(-density * stepSize);
    }

    float alpha = clamp(1.0 - transmittance, 0.0, pc.fogParams0.y);
    outColor = vec4(scattering, alpha);
}
