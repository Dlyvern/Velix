#version 450

// Glass forward pass
// Renders transparent glass meshes after the full deferred+SSR pipeline.
//
// set 0 = camera UBO  (cameraDescriptorSetLayout)
// set 1 = scene color (SSR output) + depth  (2-binding set created in GlassRenderGraphPass)
// set 2 = per-object instance SSBO           (objectDescriptorSetLayout)
//
// Push constant carries per-draw material properties (IOR, tint, frosted).

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

layout(set = 1, binding = 0) uniform sampler2D uSceneColor; // SSR / sky-light output
layout(set = 1, binding = 1) uniform sampler2D uDepth;

layout(push_constant) uniform GlassPC
{
    uint  baseInstance;
    float ior;        // index of refraction (1.5 = glass, 1.33 = water)
    float frosted;    // 0 = clear, 1 = fully frosted (uses roughnessFactor)
    float tintR;
    float tintG;
    float tintB;
    float _pad0;
    float _pad1;
} pc;

layout(location = 0) in vec2  vUV;
layout(location = 1) in vec3  vNormalView;
layout(location = 2) in vec3  vViewPos;
layout(location = 3) in vec4  vClipPos;

layout(location = 0) out vec4 outColor;

// Reconstruct view-space position from screen UV + depth.
vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 ndc  = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 vp   = camera.invProjection * ndc;
    return vp.xyz / vp.w;
}

void main()
{
    // Screen-space UV of this fragment.
    vec2 screenUV = (vClipPos.xy / vClipPos.w) * 0.5 + 0.5;

    vec3 N    = normalize(vNormalView);
    vec3 V    = normalize(-vViewPos);
    float NdotV = max(dot(N, V), 0.0);

    // ---- Refraction offset ------------------------------------------------
    // (ior - 1) drives how much the background is displaced.
    float refractionStrength = (pc.ior - 1.0) * 0.08;
    // Add frosted contribution: small random-ish wobble scales with frosted amount
    float grain = fract(sin(dot(screenUV, vec2(127.1, 311.7))) * 43758.5);
    vec2  distortion = N.xy * refractionStrength + (grain - 0.5) * pc.frosted * 0.02;

    vec2 refractUV = clamp(screenUV + distortion, 0.001, 0.999);

    // Depth test: only refract through surfaces that are behind the glass.
    float sceneDepth  = texture(uDepth, refractUV).r;
    float glassDepth  = (vClipPos.z / vClipPos.w);
    // If refracted pixel is in front of the glass, fall back to un-distorted UV.
    if (sceneDepth < glassDepth)
        refractUV = screenUV;

    vec3 refractColor = texture(uSceneColor, refractUV).rgb;

    // ---- Tint + frosted ---------------------------------------------------
    vec3 tint = vec3(pc.tintR, pc.tintG, pc.tintB);
    refractColor *= tint;
    // Frosted: blend toward a slightly blurred / white veil.
    refractColor  = mix(refractColor, tint * 0.9, pc.frosted * 0.5);

    // ---- Fresnel (Schlick) ------------------------------------------------
    float F0      = ((pc.ior - 1.0) / (pc.ior + 1.0));
    F0            = F0 * F0;   // ~0.04 for glass (ior=1.5)
    float fresnel = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);

    // Edge specular highlight (cheap substitute for reflection at grazing angles).
    vec3 edgeSpec = mix(vec3(0.0), vec3(1.0), fresnel * fresnel) * 0.25;

    vec3 finalRGB = refractColor + edgeSpec;

    // Alpha: transparent at center (NdotV~1), more opaque at grazing edges + frosted.
    float alpha = mix(0.05, 0.60, fresnel) + pc.frosted * 0.3;
    alpha = clamp(alpha, 0.0, 1.0);

    outColor = vec4(finalRGB, alpha);
}
