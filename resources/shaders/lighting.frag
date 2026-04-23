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
layout(set = 1, binding = 12) uniform sampler2D  uGIIrradiance;
layout(set = 1, binding = 13) uniform sampler2D  uBakedIrradiance;

layout(push_constant) uniform LightingPC
{
    float shadowAmbientStrength;
    float shadowMode;
    float rtShadowSamples;
    float rtShadowPenumbraSize;
    vec4  probeWorldPos_radius;
    float probeIntensity;
    float giEnabled;
    float giStrength;
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


vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}


float specularOcclusion(float NdotV, float ao, float roughness)
{
    return clamp(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
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




    float Ess = 0.04 + (1.0 - 0.04) * (1.0 - roughness);
    vec3 Favg = F0 + (1.0 - F0) * 0.047619;
    vec3 Fms = Favg * Favg * (1.0 - Ess) / (1.0 - Favg * (1.0 - Ess));
    specular *= (1.0 + Fms);

    return (diffuse + specular) * NdotL;
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
    vec3 P_world = (camera.invView * vec4(P_view, 1.0)).xyz;
    vec3 N_world = normalize((camera.invView * vec4(N_view, 0.0)).xyz);


    vec3 bakedIrradiance = texture(uBakedIrradiance, vUV).rgb;
    const bool hasBakedIrradiance = dot(bakedIrradiance, bakedIrradiance) > 0.0001;

    vec3 lighting = vec3(0.0);
    float directionalShadowMax = 0.0;
    bool hasDirectionalLight = false;
    vec3 sunDirWorld = vec3(0.0, 1.0, 0.0);
    vec3 sunColorIntensity = vec3(1.0);
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
            sunDirWorld = normalize((camera.invView * vec4(L, 0.0)).xyz);
            sunColorIntensity = light.colorStrength.rgb * light.colorStrength.a;

            if (castsShadow)
            {
                if (usePipelineRTShadows)
                {
                    shadow = sampleRTShadowFactor(i, N_view, P_view);
                }
                else
                {
                    vec3 L_world = sunDirWorld;
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

            float distRatio = distance / radius;
            float distRatio2 = distRatio * distRatio;
            float distRatio4 = distRatio2 * distRatio2;
            float attenuation = clamp(1.0 - distRatio4, 0.0, 1.0);
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

            float distRatio = distance / radius;
            float distRatio2 = distRatio * distRatio;
            float distRatio4 = distRatio2 * distRatio2;
            float attenuation = clamp(1.0 - distRatio4, 0.0, 1.0);
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


        float effectiveShadow = hasBakedIrradiance ? 0.0 : shadow;
        lighting += evaluateBRDF(N_view, V, L, albedo, metallic, roughness) * radiance * (1.0 - effectiveShadow);
    }


    float sunHeight = clamp(sunDirWorld.y, -1.0, 1.0);
    float dayFactor = smoothstep(-0.1, 0.25, sunHeight);
    float sunsetFactor = (1.0 - smoothstep(0.0, 0.40, sunHeight)) * smoothstep(-0.15, 0.02, sunHeight);
    float nightFactor = 1.0 - smoothstep(-0.15, 0.0, sunHeight);


    vec3 skyDay     = vec3(0.55, 0.70, 1.00);
    vec3 skySunset  = vec3(0.95, 0.55, 0.45);
    vec3 skyNight   = vec3(0.05, 0.07, 0.15);

    vec3 skyAmbient = mix(skyNight, skyDay, dayFactor);
    skyAmbient = mix(skyAmbient, skySunset, sunsetFactor);


    vec3 sunLuminance = sunColorIntensity * max(dayFactor + sunsetFactor * 0.7, 0.05);
    vec3 groundAmbient = mix(vec3(0.05, 0.05, 0.08), sunLuminance * vec3(0.35, 0.28, 0.22), dayFactor + sunsetFactor * 0.6);


    float ambientBrightness = 0.50 * dayFactor + 0.35 * sunsetFactor + 0.12;

    float hemiFactor = N_world.y * 0.5 + 0.5;
    vec3 hemiColor = mix(groundAmbient, skyAmbient, hemiFactor) * ambientBrightness;

    float ambientScale = hasDirectionalLight ? 1.0 : 0.7;
    vec3 ambient = albedo * hemiColor * ambientScale * ao;

    ambient *= (1.0 - clamp(pc.shadowAmbientStrength, 0.0, 1.0) * directionalShadowMax * 0.3);


    if (hasBakedIrradiance)
    {
        lighting = bakedIrradiance * albedo * ao;
    }


    if (pc.giEnabled > 0.5)
    {
        vec3 giIrradiance = texture(uGIIrradiance, vUV).rgb;
        ambient = giIrradiance * pc.giStrength * ao;
    }

    vec3 color = ambient + lighting + emissive;


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

            float NdotV = max(dot(N_world, V_world), 0.0);
            vec3 F0 = mix(vec3(0.04), albedo, metallic);

            vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);

            vec2 envBRDF = vec2(max(1.0 - roughness, F0.r), roughness);
            vec3 specWeight = F * envBRDF.x + envBRDF.y * 0.08;


            float specAO = specularOcclusion(NdotV, ao, roughness);

            color += probeColor * specWeight * pc.probeIntensity * probeInfluence * specAO;
        }
    }

    outColor = vec4(color, alpha);
}
