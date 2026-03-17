#version 450

// Contact Shadows
// Screen-space ray-march from each pixel toward the primary light direction.
// If the ray hits scene geometry, the surface is considered occluded and darkened.
//
// set 0  = camera UBO + LightSpaceUBO + LightSSBO  (cameraDescriptorSetLayout)
// set 1  = HDR lighting color, depth, GBuffer normals

const int DIRECTIONAL_LIGHT_TYPE = 0;
const int MAX_LIGHT_COUNT        = 16;
const int MAX_DIRECTIONAL_CASCADES = 4;
const int MAX_SPOT_SHADOWS       = 3;

struct Light
{
    vec4 position;
    vec4 direction;
    vec4 colorStrength;
    vec4 parameters;
    vec4 shadowInfo;
};

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
    int   lightCount;
    Light lights[];
} lightData;

layout(set = 1, binding = 0) uniform sampler2D uHDRColor;
layout(set = 1, binding = 1) uniform sampler2D uDepth;
layout(set = 1, binding = 2) uniform sampler2D uGBufferNormal;

layout(push_constant) uniform ContactShadowPC
{
    float rayLength; // world-space ray length
    float strength;  // max darkening [0, 1]
    int   steps;
    float enabled;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 ndc     = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndc;
    return viewPos.xyz / viewPos.w;
}

vec2 viewToScreenUV(vec3 viewPos)
{
    vec4 clip = camera.projection * vec4(viewPos, 1.0);
    vec3 ndc  = clip.xyz / clip.w;
    return ndc.xy * 0.5 + 0.5;
}

void main()
{
    vec4 inputColor = texture(uHDRColor, vUV);
    outColor = inputColor;

    if (pc.enabled < 0.5)
        return;

    float depth = texture(uDepth, vUV).r;
    if (depth >= 0.9999)
        return; // sky pixel

    // Only cast contact shadows on surfaces that face the light reasonably
    vec4 gN     = texture(uGBufferNormal, vUV);
    vec3 N_view = normalize(gN.rgb * 2.0 - 1.0);

    // Find the first directional light
    vec3 lightDirView = vec3(0.0, -1.0, 0.0); // default: straight down
    for (int i = 0; i < lightData.lightCount && i < MAX_LIGHT_COUNT; ++i)
    {
        if (int(lightData.lights[i].parameters.w) == DIRECTIONAL_LIGHT_TYPE)
        {
            // direction is world-space; bring to view space
            vec4 d = lightData.lights[i].direction;
            lightDirView = normalize(mat3(camera.view) * d.xyz);
            break;
        }
    }

    // Light direction points FROM light TO surface; for NdotL we want surface→light
    float NdotL = dot(N_view, -lightDirView);
    if (NdotL <= 0.0)
        return; // back-face, no shadow needed

    vec3 P_view = reconstructViewPos(vUV, depth);

    // Step in view space along the light direction (toward the light)
    vec3 stepVec = (-lightDirView) * (pc.rayLength / float(pc.steps));

    float shadow    = 0.0;
    float stepLength = pc.rayLength / max(float(pc.steps), 1.0);
    vec3  samplePos = P_view + N_view * 0.04 + (-lightDirView) * stepLength;

    for (int i = 0; i < pc.steps; ++i)
    {
        samplePos += stepVec;

        vec2 sampleUV = viewToScreenUV(samplePos);
        if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
            break;

        float sceneDepth   = texture(uDepth, sampleUV).r;
        if (sceneDepth >= 0.9999)
            continue;

        vec3  sceneViewPos = reconstructViewPos(sampleUV, sceneDepth);
        vec3  sampleNormal = normalize(texture(uGBufferNormal, sampleUV).rgb * 2.0 - 1.0);

        float depthDiff = samplePos.z - sceneViewPos.z;
        float travel = length(samplePos - P_view);
        float thickness = mix(0.01, 0.06, clamp(travel / max(pc.rayLength, 0.0001), 0.0, 1.0));
        float normalAgreement = clamp(dot(N_view, sampleNormal), 0.0, 1.0);

        // Reject self-shadowing on the same smooth surface. The old test accepted
        // a huge depth window and produced dark camera-dependent bands on spheres.
        if (normalAgreement > 0.985 && depthDiff < -0.001 && depthDiff > -thickness * 0.5)
            continue;

        // Hit: ray penetrated a nearby surface thickness in screen space.
        if (depthDiff < -0.001 && depthDiff > -thickness)
        {
            // Fade shadow toward the end of the ray for soft falloff
            float t    = float(i) / float(pc.steps - 1);
            float fade = 1.0 - t * t;
            shadow = fade;
            break;
        }
    }

    float occlusion  = shadow * pc.strength * NdotL;
    outColor.rgb     = inputColor.rgb * (1.0 - occlusion);
}
