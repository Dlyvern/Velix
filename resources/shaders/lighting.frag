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
    vec4 position;      // xyz in view space
    vec4 direction;     // xyz in view space
    vec4 colorStrength; // rgb=color, a=intensity
    vec4 parameters;    // x=inner, y=outer, z=radius, w=type
    vec4 shadowInfo;    // x=castsShadow, y=shadowIndex, z=far/range, w=near
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

layout(set = 1, binding = 0) uniform sampler2D uGBufferNormal;
layout(set = 1, binding = 1) uniform sampler2D uGBufferAlbedo;
layout(set = 1, binding = 2) uniform sampler2D uGBufferMaterial;
layout(set = 1, binding = 4) uniform sampler2D uGBufferEmissive;
layout(set = 1, binding = 5) uniform sampler2D uDepth;
layout(set = 1, binding = 6) uniform sampler2DArray directionalShadowMaps;
layout(set = 1, binding = 7) uniform sampler2DArray spotShadowMaps;
layout(set = 1, binding = 8) uniform samplerCubeArray cubeShadowMaps;
layout(set = 1, binding = 9) uniform sampler2D uSSAO;
layout(set = 1, binding = 10) uniform sampler2DArray uRTShadowFactors;
layout(set = 1, binding = 11) uniform samplerCube uProbeEnv;

layout(push_constant) uniform LightingPC
{
    float shadowAmbientStrength;
    float shadowMode;             // 0.0 = shadow maps, 2.0 = RT pipeline texture
    float rtShadowSamples;
    float rtShadowPenumbraSize;   // offset 12 — next vec4 starts at offset 16
    vec4  probeWorldPos_radius;   // xyz=probe world pos, w=radius (0=inactive)
    float probeIntensity;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

vec3 reconstructViewPosition(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndc;
    return viewPos.xyz / max(viewPos.w, 0.000001);
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

float calculateDirectionalLightShadow(int cascadeIndex, vec3 worldPos, vec3 lightDirWorld, vec3 normalWorld)
{
    vec4 posLS = lightSpaceData.directionalLightSpaceMatrices[cascadeIndex] * vec4(worldPos, 1.0);
    vec3 projCoords = posLS.xyz / posLS.w;
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (texCoords.x < 0.0 || texCoords.x > 1.0 ||
        texCoords.y < 0.0 || texCoords.y > 1.0 || currentDepth < 0.0 || currentDepth > 1.0)
        return 0.0;

    float cosNL = max(dot(normalWorld, lightDirWorld), 0.0);
    float bias = max(0.0006 * (1.0 - cosNL), 0.00005);

    float kernelScale = 1.0 + float(cascadeIndex) * 0.75;
    float shadow = 0.0;
    vec2 texelSize = (1.0 / vec2(textureSize(directionalShadowMaps, 0).xy)) * kernelScale;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(directionalShadowMaps, vec3(texCoords + vec2(x, y) * texelSize, float(cascadeIndex))).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

float getDirectionalShadowBlended(float viewDepth, vec3 worldPos, vec3 L_world, vec3 N_world)
{
    const float blendFraction = 0.20;

    float cascadeStart[4] = float[4](0.0,
        lightSpaceData.directionalCascadeSplits.x,
        lightSpaceData.directionalCascadeSplits.y,
        lightSpaceData.directionalCascadeSplits.z);
    float cascadeEnd[4] = float[4](
        lightSpaceData.directionalCascadeSplits.x,
        lightSpaceData.directionalCascadeSplits.y,
        lightSpaceData.directionalCascadeSplits.z,
        lightSpaceData.directionalCascadeSplits.z * 4.0);

    int cascade = selectDirectionalCascade(viewDepth);
    float shadow = calculateDirectionalLightShadow(cascade, worldPos, L_world, N_world);

    if (cascade < 3)
    {
        float rangeStart = cascadeStart[cascade];
        float rangeEnd   = cascadeEnd[cascade];
        float blendStart = mix(rangeStart, rangeEnd, 1.0 - blendFraction);
        if (viewDepth > blendStart)
        {
            float t = clamp((viewDepth - blendStart) / max(rangeEnd - blendStart, 0.0001), 0.0, 1.0);
            t = smoothstep(0.0, 1.0, t);
            float shadowNext = calculateDirectionalLightShadow(cascade + 1, worldPos, L_world, N_world);
            shadow = mix(shadow, shadowNext, t);
        }
    }

    return shadow;
}

float calculateSpotLightShadow(int shadowIndex, vec3 worldPos, vec3 lightDirWorld, vec3 normalWorld)
{
    if (shadowIndex < 0 || shadowIndex >= MAX_SPOT_SHADOWS)
        return 0.0;

    vec4 posLS = lightSpaceData.spotLightSpaceMatrices[shadowIndex] * vec4(worldPos, 1.0);
    vec3 projCoords = posLS.xyz / posLS.w;
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (texCoords.x < 0.0 || texCoords.x > 1.0 ||
        texCoords.y < 0.0 || texCoords.y > 1.0 || currentDepth < 0.0 || currentDepth > 1.0)
        return 0.0;

    float cosNL = max(dot(normalWorld, lightDirWorld), 0.0);
    float bias = max(0.0006 * (1.0 - cosNL), 0.00005);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(spotShadowMaps, 0).xy);
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(spotShadowMaps, vec3(texCoords + vec2(x, y) * texelSize, float(shadowIndex))).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

float calculatePointLightShadow(int shadowIndex, vec3 worldPos, vec3 normalWorld, vec3 lightPosWorld, float farPlane, float nearPlane)
{
    if (shadowIndex < 0 || farPlane <= nearPlane || nearPlane <= 0.0)
        return 0.0;

    vec3 toFragment = worldPos - lightPosWorld;
    float currentDepth = length(toFragment);
    if (currentDepth <= 0.0 || currentDepth >= farPlane)
        return 0.0;

    vec3 lightDir = normalize(lightPosWorld - worldPos);
    float normalBias = max(0.02 * (1.0 - max(dot(normalWorld, lightDir), 0.0)), 0.002);

    const vec3 sampleOffsets[8] = vec3[](
        vec3(1.0,  1.0,  1.0),
        vec3(-1.0, 1.0,  1.0),
        vec3(1.0, -1.0,  1.0),
        vec3(-1.0,-1.0,  1.0),
        vec3(1.0,  1.0, -1.0),
        vec3(-1.0, 1.0, -1.0),
        vec3(1.0, -1.0, -1.0),
        vec3(-1.0,-1.0, -1.0));

    float shadow = 0.0;
    float sampleRadius = 0.03 * (currentDepth / max(farPlane, 0.001));

    for (int i = 0; i < 8; ++i)
    {
        vec3 sampleDirection = normalize(toFragment + sampleOffsets[i] * sampleRadius);
        float sampledDepth = texture(cubeShadowMaps, vec4(sampleDirection, float(shadowIndex))).r;

        float denom = max(farPlane - sampledDepth * (farPlane - nearPlane), 0.0001);
        float closestDepth = (nearPlane * farPlane) / denom;
        shadow += (currentDepth - normalBias) > closestDepth ? 1.0 : 0.0;
    }

    return shadow / 8.0;
}

vec3 computeSpecular(vec3 N, vec3 V, vec3 L, vec3 specularColor, float roughness)
{
    vec3 H = normalize(V + L);
    float shininess = mix(128.0, 8.0, clamp(roughness, 0.0, 1.0));
    float specularStrength = pow(max(dot(N, H), 0.0), shininess);
    specularStrength *= mix(1.0, 0.15, clamp(roughness, 0.0, 1.0));
    return specularColor * specularStrength;
}

vec3 decodeNormal(vec3 encodedNormal)
{
    vec3 normal = encodedNormal * 2.0 - 1.0;
    float normalLength = length(normal);
    if (normalLength < 0.00001)
        return vec3(0.0, 0.0, 1.0);
    return normal / normalLength;
}

float sampleRTShadowFactor(int lightIndex, vec3 centerNormalView, vec3 centerViewPos)
{
    ivec3 shadowSize = textureSize(uRTShadowFactors, 0);
    if (lightIndex < 0 || lightIndex >= shadowSize.z || shadowSize.x <= 0 || shadowSize.y <= 0)
        return 0.0;

    ivec2 fullResSize = textureSize(uDepth, 0);
    if (shadowSize.xy == fullResSize)
    {
        ivec2 pixelCoord = clamp(ivec2(vUV * vec2(fullResSize)), ivec2(0), fullResSize - ivec2(1));
        return texelFetch(uRTShadowFactors, ivec3(pixelCoord, lightIndex), 0).r;
    }

    vec2 shadowSizeF = vec2(shadowSize.xy);
    vec2 shadowCoord = vUV * shadowSizeF - 0.5;
    ivec2 baseCoord = ivec2(floor(shadowCoord));
    vec2 fracCoord = fract(shadowCoord);

    float weightedShadow = 0.0;
    float weightSum = 0.0;

    for (int y = 0; y <= 1; ++y)
    {
        for (int x = 0; x <= 1; ++x)
        {
            ivec2 tapCoord = clamp(baseCoord + ivec2(x, y), ivec2(0), shadowSize.xy - ivec2(1));
            vec2 tapUV = (vec2(tapCoord) + 0.5) / shadowSizeF;

            float bilinearWeight =
                (x == 0 ? (1.0 - fracCoord.x) : fracCoord.x) *
                (y == 0 ? (1.0 - fracCoord.y) : fracCoord.y);

            float tapDepth = texture(uDepth, tapUV).r;
            if (tapDepth >= 1.0)
                continue;

            vec3 tapNormalView = decodeNormal(texture(uGBufferNormal, tapUV).rgb);
            vec3 tapViewPos = reconstructViewPosition(tapUV, tapDepth);

            float normalWeight = exp(-(1.0 - clamp(dot(centerNormalView, tapNormalView), 0.0, 1.0)) / 0.08);
            float depthSigma = max(0.2, 0.02 * abs(centerViewPos.z));
            float depthWeight = exp(-abs(tapViewPos.z - centerViewPos.z) / depthSigma);
            float weight = bilinearWeight * normalWeight * depthWeight;

            if (weight <= 0.00001)
                continue;

            float tapShadow = texelFetch(uRTShadowFactors, ivec3(tapCoord, lightIndex), 0).r;
            weightedShadow += tapShadow * weight;
            weightSum += weight;
        }
    }

    if (weightSum > 0.00001)
        return weightedShadow / weightSum;

    ivec2 nearestCoord = clamp(ivec2(floor(shadowCoord + 0.5)), ivec2(0), shadowSize.xy - ivec2(1));
    return texelFetch(uRTShadowFactors, ivec3(nearestCoord, lightIndex), 0).r;
}

void main()
{
    vec4 gN = texture(uGBufferNormal, vUV);
    vec4 gA = texture(uGBufferAlbedo, vUV);
    vec4 gM = texture(uGBufferMaterial, vUV);
    vec3 emissive = texture(uGBufferEmissive, vUV).rgb;
    float ssaoAO = clamp(texture(uSSAO, vUV).r, 0.0, 1.0);
    float depth = texture(uDepth, vUV).r;

    if (depth >= 1.0)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 N_view = decodeNormal(gN.rgb);
    vec3 albedo = gA.rgb;
    float alpha = gA.a;
    float ao = clamp(gM.r * ssaoAO, 0.0, 1.0);
    float roughness = clamp(gM.g, 0.04, 1.0);
    float metallic = clamp(gM.b, 0.0, 1.0);

    vec3 P_view = reconstructViewPosition(vUV, depth);
    vec3 V = normalize(-P_view);
    vec3 specularColor = mix(vec3(0.04), albedo, metallic);

    vec3 P_world = (camera.invView * vec4(P_view, 1.0)).xyz;
    vec3 N_world = normalize((camera.invView * vec4(N_view, 0.0)).xyz);

    vec3 lighting = vec3(0.0);
    float directionalShadowMax = 0.0;
    bool hasDirectionalLight = false;
    const bool usePipelineRTShadows = pc.shadowMode > 1.5;

    int count = min(lightData.lightCount, MAX_LIGHT_COUNT);
    for (int i = 0; i < count; ++i)
    {
        Light light = lightData.lights[i];
        int lightType = int(light.parameters.w);

        if (lightType == DIRECTIONAL_LIGHT_TYPE)
            hasDirectionalLight = true;

        vec3 L;
        float shadow = 0.0;
        bool castsShadow = light.shadowInfo.x > 0.5;
        int shadowIndex = int(light.shadowInfo.y);
        float shadowFar = light.shadowInfo.z;
        float shadowNear = light.shadowInfo.w;
        vec3 radiance = light.colorStrength.rgb * light.colorStrength.a;

        if (lightType == DIRECTIONAL_LIGHT_TYPE)
        {
            L = normalize(-light.direction.xyz);

            if (castsShadow)
            {
                if (usePipelineRTShadows)
                {
                    shadow = sampleRTShadowFactor(i, N_view, P_view);
                }
                else
                {
                    vec3 L_world = normalize((camera.invView * vec4(L, 0.0)).xyz);
                    shadow = getDirectionalShadowBlended(max(-P_view.z, 0.0), P_world, L_world, N_world);
                }
                directionalShadowMax = max(directionalShadowMax, shadow);
            }
        }
        else if (lightType == POINT_LIGHT_TYPE)
        {
            vec3 toLight = light.position.xyz - P_view;
            float distance = length(toLight);
            L = (distance > 0.0) ? toLight / distance : vec3(0.0, 0.0, 1.0);

            float radius = max(light.parameters.z, 0.0001);
            float attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
            attenuation *= attenuation;
            radiance *= attenuation;

            if (castsShadow)
            {
                if (usePipelineRTShadows)
                {
                    shadow = sampleRTShadowFactor(i, N_view, P_view);
                }
                else
                {
                    vec3 lightPosWorld = (camera.invView * vec4(light.position.xyz, 1.0)).xyz;
                    shadow = calculatePointLightShadow(shadowIndex, P_world, N_world, lightPosWorld, shadowFar, shadowNear);
                }
            }
        }
        else if (lightType == SPOT_LIGHT_TYPE)
        {
            vec3 toLight = light.position.xyz - P_view;
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

            radiance *= attenuation * spot;

            if (castsShadow)
            {
                if (usePipelineRTShadows)
                {
                    shadow = sampleRTShadowFactor(i, N_view, P_view);
                }
                else
                {
                    vec3 L_world = normalize((camera.invView * vec4(L, 0.0)).xyz);
                    shadow = calculateSpotLightShadow(shadowIndex, P_world, L_world, N_world);
                }
            }
        }
        else
            continue;

        float NdotL = max(dot(N_view, L), 0.0);
        if (NdotL <= 0.0)
            continue;

        vec3 diffuseColor = mix(albedo, vec3(0.0), metallic);
        vec3 diffuse = (diffuseColor / PI) * NdotL;
        vec3 specular = computeSpecular(N_view, V, L, specularColor, roughness) * NdotL;

        lighting += (diffuse + specular) * radiance * (1.0 - shadow);
    }

    float ambientFactor = hasDirectionalLight ? 0.03 : 0.0;
    vec3 ambient = albedo * ambientFactor * ao;
    ambient *= (1.0 - clamp(pc.shadowAmbientStrength, 0.0, 1.0) * directionalShadowMax);

    vec3 color = ambient + lighting + emissive;

    // Reflection probe: local environment specular contribution
    if (pc.probeWorldPos_radius.w > 0.001)
    {
        float distToProbe = length(P_world - pc.probeWorldPos_radius.xyz);
        float probeInfluence = 1.0 - smoothstep(0.0, pc.probeWorldPos_radius.w, distToProbe);
        if (probeInfluence > 0.001)
        {
            vec3 V_world = normalize((camera.invView * vec4(V, 0.0)).xyz);
            vec3 R_world = reflect(-V_world, N_world);
            float mipLevel = roughness * float(max(textureQueryLevels(uProbeEnv) - 1, 0));
            vec3 probeColor = textureLod(uProbeEnv, R_world, mipLevel).rgb;

            float NdotV    = max(dot(N_view, V), 0.0);
            float F0       = mix(0.04, 1.0, metallic);
            float fresnel  = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
            float specMask = (1.0 - roughness * roughness);

            color += probeColor * fresnel * specMask * pc.probeIntensity * probeInfluence * ao;
        }
    }

    outColor = vec4(color, alpha);
}
