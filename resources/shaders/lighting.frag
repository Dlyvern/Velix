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
layout(set = 1, binding = 3) uniform sampler2D uGBufferTangentAniso;
layout(set = 1, binding = 4) uniform sampler2D uDepth;
layout(set = 1, binding = 5) uniform sampler2DArray directionalShadowMaps;
layout(set = 1, binding = 6) uniform sampler2DArray spotShadowMaps;
layout(set = 1, binding = 7) uniform samplerCubeArray cubeShadowMaps;
layout(set = 1, binding = 8) uniform sampler2D uSSAO; // rgb=bent normal(enc), a=ao

// IBL textures (set 2)
layout(set = 2, binding = 0) uniform samplerCube uIrradianceMap;
layout(set = 2, binding = 1) uniform sampler2D   uBRDFLUT;
layout(set = 2, binding = 2) uniform samplerCube uEnvMap;

layout(push_constant) uniform LightingPC {
    float iblEnabled;
    float iblDiffuseIntensity;
    float iblSpecularIntensity;
    float useBentNormals;
    float enableAnisotropy;
    float anisotropyStrength;
    float anisotropyRotationRadians;
    float shadowAmbientStrength;
} pc;

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

float DistributionGGXAniso(vec3 N, vec3 H, vec3 T, vec3 B, float ax, float ay)
{
    float NdotH = max(dot(N, H), 0.0);
    float TdotH = dot(T, H);
    float BdotH = dot(B, H);

    float ax2 = ax * ax;
    float ay2 = ay * ay;
    float denom = (TdotH * TdotH) / max(ax2, 0.000001) +
                  (BdotH * BdotH) / max(ay2, 0.000001) +
                  NdotH * NdotH;

    return 1.0 / max(PI * ax * ay * denom * denom, 0.000001);
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

float GeometrySmithG1Aniso(vec3 N, vec3 V, vec3 T, vec3 B, float ax, float ay)
{
    float NdotV = max(dot(N, V), 0.0001);
    float TdotV = dot(T, V);
    float BdotV = dot(B, V);

    float lambda = (-1.0 + sqrt(1.0 +
        ((TdotV * TdotV) * (ax * ax) + (BdotV * BdotV) * (ay * ay)) / (NdotV * NdotV))) * 0.5;

    return 1.0 / (1.0 + max(lambda, 0.0));
}

float GeometrySmithAniso(vec3 N, vec3 V, vec3 L, vec3 T, vec3 B, float ax, float ay)
{
    return GeometrySmithG1Aniso(N, V, T, B, ax, ay) *
           GeometrySmithG1Aniso(N, L, T, B, ax, ay);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

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
    for (int y = -1; y <= 1; ++y)
    {
        float pcfDepth = texture(spotShadowMaps, vec3(texCoords + vec2(x, y) * texelSize, float(shadowIndex))).r;
        shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
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

vec3 orthonormalizeTangent(vec3 N, vec3 T)
{
    T = normalize(T - N * dot(N, T));
    if (length(T) < 0.001)
    {
        vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
        T = normalize(cross(up, N));
    }
    return T;
}

void main()
{
    vec4 gN = texture(uGBufferNormal, vUV);
    vec4 gA = texture(uGBufferAlbedo, vUV);
    vec4 gM = texture(uGBufferMaterial, vUV);
    vec4 gT = texture(uGBufferTangentAniso, vUV);
    vec4 aoBent = texture(uSSAO, vUV);
    float depth = texture(uDepth, vUV).r;

    if (depth >= 1.0)
    {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 N_view = normalize(gN.rgb * 2.0 - 1.0);
    vec3 T_view = orthonormalizeTangent(N_view, gT.rgb * 2.0 - 1.0);
    vec3 B_view = normalize(cross(N_view, T_view));

    if (pc.enableAnisotropy > 0.5)
    {
        float c = cos(pc.anisotropyRotationRadians);
        float s = sin(pc.anisotropyRotationRadians);
        T_view = normalize(c * T_view + s * B_view);
        B_view = normalize(cross(N_view, T_view));
    }

    vec3 albedo = gA.rgb;
    float alpha = gA.a;

    float ssaoAO = clamp(aoBent.a, 0.0, 1.0);
    float ao = clamp(gM.r * ssaoAO, 0.0, 1.0);
    float roughness = clamp(gM.g, 0.04, 1.0);
    float metallic = clamp(gM.b, 0.0, 1.0);
    float emissiveStrength = gM.a;

    vec3 bentView = normalize(aoBent.rgb * 2.0 - 1.0);
    if (length(bentView) < 0.1 || pc.useBentNormals < 0.5)
        bentView = N_view;

    vec3 P_view = reconstructViewPosition(vUV, depth);
    vec3 V = normalize(-P_view);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 Lo = vec3(0.0);

    vec3 P_world = (camera.invView * vec4(P_view, 1.0)).xyz;
    vec3 N_world = normalize((camera.invView * vec4(N_view, 0.0)).xyz);

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

            if (castsShadow)
            {
                int cascadeIndex = selectDirectionalCascade(max(-P_view.z, 0.0));
                vec3 L_world = normalize((camera.invView * vec4(L, 0.0)).xyz);
                shadow = calculateDirectionalLightShadow(cascadeIndex, P_world, L_world, N_world);
                directionalShadowMax = max(directionalShadowMax, shadow);
            }
        }
        else if (lightType == POINT_LIGHT_TYPE)
        {
            vec3 toLight = light.position.xyz - P_view;
            float d = length(toLight);
            L = (d > 0.0) ? (toLight / d) : vec3(0.0, 0.0, 1.0);

            float radius = max(light.parameters.z, 0.0001);
            float att = clamp(1.0 - (d / radius), 0.0, 1.0);
            att *= att;
            radiance *= att;

            if (castsShadow)
            {
                vec3 lightPosWorld = (camera.invView * vec4(light.position.xyz, 1.0)).xyz;
                shadow = calculatePointLightShadow(shadowIndex, P_world, N_world, lightPosWorld, shadowFar, shadowNear);
            }
        }
        else if (lightType == SPOT_LIGHT_TYPE)
        {
            vec3 toLight = light.position.xyz - P_view;
            float d = length(toLight);
            L = (d > 0.0) ? (toLight / d) : vec3(0.0, 0.0, 1.0);

            float radius = max(light.parameters.z, 0.0001);
            float att = clamp(1.0 - (d / radius), 0.0, 1.0);
            att *= att;

            float theta = dot(L, normalize(-light.direction.xyz));
            float innerCutoff = light.parameters.x;
            float outerCutoff = light.parameters.y;
            float epsilon = max(innerCutoff - outerCutoff, 0.0001);
            float spot = clamp((theta - outerCutoff) / epsilon, 0.0, 1.0);

            radiance *= att * spot;

            if (castsShadow)
            {
                vec3 L_world = normalize((camera.invView * vec4(L, 0.0)).xyz);
                shadow = calculateSpotLightShadow(shadowIndex, P_world, L_world, N_world);
            }
        }
        else
        {
            continue;
        }

        float NdotL = max(dot(N_view, L), 0.0);
        float NdotV = max(dot(N_view, V), 0.0);
        if (NdotL <= 0.0 || NdotV <= 0.0)
            continue;

        vec3 H = normalize(V + L);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        float NDF = 0.0;
        float G = 0.0;

        if (pc.enableAnisotropy > 0.5)
        {
            float a = max(roughness * roughness, 0.001);
            float aniso = clamp(pc.anisotropyStrength, -0.95, 0.95);
            float aspect = sqrt(1.0 - 0.9 * aniso);
            float ax = max(0.001, a / aspect);
            float ay = max(0.001, a * aspect);

            NDF = DistributionGGXAniso(N_view, H, T_view, B_view, ax, ay);
            G = GeometrySmithAniso(N_view, V, L, T_view, B_view, ax, ay);
        }
        else
        {
            NDF = DistributionGGX(N_view, H, roughness);
            G = GeometrySmith(N_view, V, L, roughness);
        }

        vec3 specular = (NDF * G * F) / max(4.0 * NdotV * NdotL, 0.0001);

        vec3 kS = F;
        vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
        vec3 diffuse = (kD * albedo) / PI;

        vec3 lightContrib = (diffuse + specular) * radiance * NdotL;
        lightContrib *= (1.0 - shadow);

        Lo += lightContrib;
    }

    vec3 ambient;
    if (pc.iblEnabled > 0.5)
    {
        vec3 N_ambient_view = pc.useBentNormals > 0.5 ? bentView : N_view;
        vec3 N_ambient_world = normalize((camera.invView * vec4(N_ambient_view, 0.0)).xyz);
        vec3 N_spec_world = N_world;
        vec3 V_world = normalize((camera.invView * vec4(V, 0.0)).xyz);
        float NdotV = max(dot(N_spec_world, V_world), 0.0);

        vec3 irradiance = texture(uIrradianceMap, N_ambient_world).rgb;
        vec3 F_amb = FresnelSchlick(NdotV, F0);
        vec3 kD_amb = (vec3(1.0) - F_amb) * (1.0 - metallic);
        vec3 diffuseIBL = kD_amb * albedo * irradiance * pc.iblDiffuseIntensity;

        vec3 R_world = reflect(-V_world, N_spec_world);
        vec2 brdf = texture(uBRDFLUT, vec2(NdotV, roughness)).rg;
        vec3 prefilter = textureLod(uEnvMap, R_world, roughness * 4.0).rgb;
        vec3 specularIBL = prefilter * (F_amb * brdf.x + brdf.y) * pc.iblSpecularIntensity;

        ambient = (diffuseIBL + specularIBL) * ao;
    }
    else
    {
        float ambientFactor = hasDirectionalLight ? 0.03 : 0.0;
        ambient = albedo * ambientFactor * ao;
    }

    ambient *= (1.0 - clamp(pc.shadowAmbientStrength, 0.0, 1.0) * directionalShadowMax);

    vec3 emissive = vec3(emissiveStrength);
    vec3 color = ambient + Lo + emissive;

    outColor = vec4(color, alpha);
}
