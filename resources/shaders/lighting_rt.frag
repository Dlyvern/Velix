#version 460
#extension GL_EXT_ray_query : require

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

layout(set = 0, binding = 3) uniform accelerationStructureEXT uTLAS;

layout(set = 1, binding = 0) uniform sampler2D uGBufferNormal;
layout(set = 1, binding = 1) uniform sampler2D uGBufferAlbedo;
layout(set = 1, binding = 2) uniform sampler2D uGBufferMaterial;
layout(set = 1, binding = 3) uniform sampler2D uGBufferTangentAniso;
layout(set = 1, binding = 4) uniform sampler2D uGBufferEmissive;
layout(set = 1, binding = 5) uniform sampler2D uDepth;
layout(set = 1, binding = 6) uniform sampler2DArray directionalShadowMaps;
layout(set = 1, binding = 7) uniform sampler2DArray spotShadowMaps;
layout(set = 1, binding = 8) uniform samplerCubeArray cubeShadowMaps;
layout(set = 1, binding = 9) uniform sampler2D uSSAO;

layout(push_constant) uniform LightingPC
{
    float shadowAmbientStrength;
    float enableRTShadows;      // 1.0 = ray query shadows, 0.0 = shadow maps
    float rtShadowSamples;      // number of rays per light (1 = hard, 4-16 = soft)
    float rtShadowPenumbraSize; // virtual light radius — larger = wider penumbra
} pc;

// ---------------------------------------------------------------------------
// SINGLE shadow ray — binary occlusion test.
//
// Returns 1.0 if anything blocks the path (in shadow), 0.0 if clear (lit).
//
// Flags:
//   TerminateOnFirstHit  — stop at first hit, we don't need the closest
//   SkipClosestHitShader — no need to shade the hit, just detect it
//   OpaqueEXT            — only test opaque geometry
// ---------------------------------------------------------------------------
float traceShadowRay(vec3 origin, vec3 direction, float tMax)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq, uTLAS,
        gl_RayFlagsTerminateOnFirstHitEXT |
        gl_RayFlagsSkipClosestHitShaderEXT |
        gl_RayFlagsOpaqueEXT,
        0xFF,   // cullMask: all instances
        origin,
        0.001,  // tMin: 1 mm offset, backup against self-intersection
        direction,
        tMax);

    while (rayQueryProceedEXT(rq)) {}

    return (rayQueryGetIntersectionTypeEXT(rq, true) !=
            gl_RayQueryCommittedIntersectionNoneEXT) ? 1.0 : 0.0;
}

// Concept: instead of 1 ray toward the exact light direction, fire N rays
// spread across a virtual light disk.  The fraction that hit geometry is
// the shadow value — partial hits give the penumbra.

void buildOrthonormalBasis(vec3 n, out vec3 tangent, out vec3 bitangent)
{
    // Frisvad method: numerically stable for any normal direction.
    if (n.z < -0.9999)
    {
        tangent   = vec3( 0.0, -1.0, 0.0);
        bitangent = vec3(-1.0,  0.0, 0.0);
        return;
    }
    float a = 1.0 / (1.0 + n.z);
    float b = -n.x * n.y * a;
    tangent   = vec3(1.0 - n.x * n.x * a, b, -n.x);
    bitangent = vec3(b, 1.0 - n.y * n.y * a, -n.y);
}

// Interleaved Gradient Noise — returns [0, 1] per screen pixel.
// Uses gl_FragCoord, so automatically pixel-unique.
float interleavedGradientNoise()
{
    vec2 p = gl_FragCoord.xy;
    // Magic constants from Jorge Jimenez (2014).
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// Vogel disk: places `sampleIndex` of `samplesCount` uniformly in a unit disk.
// `phi` rotates the whole pattern (use IGN so each pixel differs).
vec2 vogelDiskSample(int sampleIndex, int samplesCount, float phi)
{
    // Golden angle in radians ≈ 2.399963.
    // Consecutive samples are separated by exactly this angle → uniform spread.
    const float goldenAngle = 2.4;
    float r     = sqrt(float(sampleIndex) + 0.5) / sqrt(float(samplesCount));
    float theta = float(sampleIndex) * goldenAngle + phi;
    return vec2(r * cos(theta), r * sin(theta));
}

// Jitters the DIRECTION on a disk of angular radius `penumbraSize` (world
// units at 1 m) perpendicular to `baseDir`.  Works well for sun-like lights
// where the source is at infinity.
float traceSoftShadowDir(vec3 origin, vec3 baseDir, float tMax,
                         float penumbraSize, int numSamples)
{
    if (numSamples <= 1)
        return traceShadowRay(origin, baseDir, tMax);

    vec3 tangent, bitangent;
    buildOrthonormalBasis(baseDir, tangent, bitangent);

    float phi    = interleavedGradientNoise() * 2.0 * PI;
    float shadow = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        // Pick a point on the virtual light disk.
        vec2  disk       = vogelDiskSample(i, numSamples, phi) * penumbraSize;
        // Offset the direction by that disk point, then re-normalise.
        vec3  jitteredDir = normalize(baseDir + tangent * disk.x + bitangent * disk.y);
        shadow += traceShadowRay(origin, jitteredDir, tMax);
    }

    return shadow / float(numSamples);
}

// Jitters the TARGET POINT on a disk centred on the light position.
// Physically more accurate for local lights: the penumbra grows with
// distance from the occluder.
float traceSoftShadowPoint(vec3 origin, vec3 lightPosWorld,
                           float penumbraSize, int numSamples)
{
    vec3  toLight   = lightPosWorld - origin;
    float dist      = length(toLight);
    vec3  baseDir   = toLight / max(dist, 0.0001);

    if (numSamples <= 1)
        return traceShadowRay(origin, baseDir, dist * 0.999);

    vec3 tangent, bitangent;
    buildOrthonormalBasis(baseDir, tangent, bitangent);

    float phi    = interleavedGradientNoise() * 2.0 * PI;
    float shadow = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        // Sample a random point on the virtual light surface.
        vec2  disk           = vogelDiskSample(i, numSamples, phi) * penumbraSize;
        vec3  jitteredTarget = lightPosWorld + tangent * disk.x + bitangent * disk.y;
        vec3  jitteredDir    = jitteredTarget - origin;
        float jitteredDist   = length(jitteredDir);
        shadow += traceShadowRay(origin, jitteredDir / max(jitteredDist, 0.0001),
                                 jitteredDist * 0.999);
    }

    return shadow / float(numSamples);
}

vec3 shadowOrigin(vec3 P_world, vec3 N_world, float NdotL)
{
    float bias = mix(0.02, 0.005, clamp(NdotL, 0.0, 1.0));
    return P_world + N_world * bias;
}

vec3 reconstructViewPosition(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndc;
    return viewPos.xyz / max(viewPos.w, 0.000001);
}

int selectDirectionalCascade(float viewDepth)
{
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.x) return 0;
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.y) return 1;
    if (viewDepth <= lightSpaceData.directionalCascadeSplits.z) return 2;
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

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(directionalShadowMaps, 0).xy);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(directionalShadowMaps, vec3(texCoords + vec2(x, y) * texelSize, float(cascadeIndex))).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

float calculateSpotLightShadow(int shadowIndex, vec3 worldPos, vec3 lightDirWorld, vec3 normalWorld)
{
    if (shadowIndex < 0 || shadowIndex >= MAX_SPOT_SHADOWS) return 0.0;

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
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(spotShadowMaps, vec3(texCoords + vec2(x, y) * texelSize, float(shadowIndex))).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
        }
    return shadow / 9.0;
}

float calculatePointLightShadow(int shadowIndex, vec3 worldPos, vec3 normalWorld, vec3 lightPosWorld, float farPlane, float nearPlane)
{
    if (shadowIndex < 0 || farPlane <= nearPlane || nearPlane <= 0.0) return 0.0;

    vec3 toFragment = worldPos - lightPosWorld;
    float currentDepth = length(toFragment);
    if (currentDepth <= 0.0 || currentDepth >= farPlane) return 0.0;

    vec3 lightDir = normalize(lightPosWorld - worldPos);
    float normalBias = max(0.02 * (1.0 - max(dot(normalWorld, lightDir), 0.0)), 0.002);

    const vec3 sampleOffsets[8] = vec3[](
        vec3( 1.0,  1.0,  1.0), vec3(-1.0,  1.0,  1.0),
        vec3( 1.0, -1.0,  1.0), vec3(-1.0, -1.0,  1.0),
        vec3( 1.0,  1.0, -1.0), vec3(-1.0,  1.0, -1.0),
        vec3( 1.0, -1.0, -1.0), vec3(-1.0, -1.0, -1.0));

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

void main()
{
    vec4 gN = texture(uGBufferNormal, vUV);
    vec4 gA = texture(uGBufferAlbedo, vUV);
    vec4 gM = texture(uGBufferMaterial, vUV);
    vec3 emissive = texture(uGBufferEmissive, vUV).rgb;
    float ssaoAO = clamp(texture(uSSAO, vUV).a, 0.0, 1.0);
    float depth = texture(uDepth, vUV).r;

    if (depth >= 1.0)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 N_view = normalize(gN.rgb * 2.0 - 1.0);
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

    const bool useRT = pc.enableRTShadows > 0.5;

    vec3 lighting = vec3(0.0);
    float directionalShadowMax = 0.0;
    bool hasDirectionalLight = false;

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
            vec3 L_world = normalize((camera.invView * vec4(L, 0.0)).xyz);

            if (castsShadow)
            {
                if (useRT)
                {
                    float NdotL = max(dot(N_view, L), 0.0);
                    int   nSamp = max(int(pc.rtShadowSamples), 1);
                    // Directional: jitter the direction on a cone.
                    // penumbraSize controls the angular spread of the sun disk.
                    shadow = traceSoftShadowDir(shadowOrigin(P_world, N_world, NdotL),
                                               L_world, 10000.0,
                                               pc.rtShadowPenumbraSize, nSamp);
                }
                else
                {
                    int cascadeIndex = selectDirectionalCascade(max(-P_view.z, 0.0));
                    shadow = calculateDirectionalLightShadow(cascadeIndex, P_world, L_world, N_world);
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
                if (useRT)
                {
                    vec3  lightPosWorld = (camera.invView * vec4(light.position.xyz, 1.0)).xyz;
                    float NdotL        = max(dot(N_view, L), 0.0);
                    int   nSamp        = max(int(pc.rtShadowSamples), 1);
                    // Point light: jitter the target point on a disk around
                    // the light — physically models a spherical light source.
                    shadow = traceSoftShadowPoint(shadowOrigin(P_world, N_world, NdotL),
                                                 lightPosWorld,
                                                 pc.rtShadowPenumbraSize, nSamp);
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
                if (useRT)
                {
                    vec3  lightPosWorld = (camera.invView * vec4(light.position.xyz, 1.0)).xyz;
                    float NdotL        = max(dot(N_view, L), 0.0);
                    int   nSamp        = max(int(pc.rtShadowSamples), 1);
                    // Spot light: same disk-jitter as point light — the
                    // cone cutoff already handles the spot shape.
                    shadow = traceSoftShadowPoint(shadowOrigin(P_world, N_world, NdotL),
                                                 lightPosWorld,
                                                 pc.rtShadowPenumbraSize, nSamp);
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
    outColor = vec4(color, alpha);
}
