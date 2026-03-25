#version 450

// GBuffer outputs — must match GBuffer attachment order:
// location 0 = normal   (R16G16B16A16_SFLOAT)
// location 1 = albedo   (R8G8B8A8_UNORM)
// location 2 = material (R8G8B8A8_UNORM)
// location 3 = emissive (R16G16B16A16_SFLOAT)
layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outAlbedo;
layout(location = 2) out vec4 outMaterial;
layout(location = 3) out vec4 outEmissive;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

layout(set = 1, binding = 0) uniform sampler2D uDepth;

// materialDescriptorSetLayout: albedo(0) normal(1) orm(2) emissive(3) params(4)
layout(set = 2, binding = 0) uniform sampler2D uDecalAlbedo;
layout(set = 2, binding = 1) uniform sampler2D uDecalNormal;
layout(set = 2, binding = 2) uniform sampler2D uDecalORM;
layout(set = 2, binding = 3) uniform sampler2D uDecalEmissive;

layout(push_constant) uniform PC
{
    mat4 worldViewProj;
    mat4 worldToLocal;
    vec4 params; // x=opacity, y=invW, z=invH, w=blendMode
} pc;

const int BLEND_COLOR_NORMAL = 0;
const int BLEND_COLOR_ONLY   = 1;
const int BLEND_NORMAL_ONLY  = 2;
const int BLEND_EMISSIVE     = 3;

void main()
{
    // Reconstruct world position from the depth buffer
    vec2 screenUV = gl_FragCoord.xy * vec2(pc.params.y, pc.params.z);
    float depth = texture(uDepth, screenUV).r;

    // Discard sky fragments (no geometry)
    if (depth >= 1.0)
        discard;

    vec4 ndc     = vec4(screenUV * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndc;
    viewPos      /= viewPos.w;
    vec4 worldPos = camera.invView * vec4(viewPos.xyz, 1.0);

    // Transform to decal local space
    vec4 localPos = pc.worldToLocal * worldPos;

    // Clip to unit cube [-0.5, 0.5]^3
    if (abs(localPos.x) > 0.5001 || abs(localPos.y) > 0.5001 || abs(localPos.z) > 0.5001)
        discard;

    // Project XY → UV (Z is the projection axis, looking down -Z)
    vec2 decalUV = localPos.xy + vec2(0.5);

    float opacity   = pc.params.x;
    int   blendMode = int(pc.params.w);

    vec4 albedoSample   = texture(uDecalAlbedo,   decalUV);
    vec4 normalSample   = texture(uDecalNormal,   decalUV);
    vec4 ormSample      = texture(uDecalORM,      decalUV);
    vec4 emissiveSample = texture(uDecalEmissive, decalUV);

    float finalAlpha = albedoSample.a * opacity;

    outNormal   = vec4(normalSample.rgb,   finalAlpha);
    outAlbedo   = vec4(albedoSample.rgb,   finalAlpha);
    outMaterial = vec4(ormSample.rgb,      finalAlpha);
    outEmissive = vec4(emissiveSample.rgb, finalAlpha);

    // Zero out unused channels per blend mode
    if (blendMode == BLEND_COLOR_ONLY)
    {
        outNormal.a   = 0.0;
        outMaterial.a = 0.0;
        outEmissive.a = 0.0;
    }
    else if (blendMode == BLEND_NORMAL_ONLY)
    {
        outAlbedo.a   = 0.0;
        outMaterial.a = 0.0;
        outEmissive.a = 0.0;
    }
    else if (blendMode == BLEND_EMISSIVE)
    {
        outAlbedo.a   = 0.0;
        outNormal.a   = 0.0;
        outMaterial.a = 0.0;
    }
}
