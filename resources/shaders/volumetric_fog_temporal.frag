#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

layout(set = 1, binding = 0) uniform sampler2D uCurrentFog;
layout(set = 1, binding = 1) uniform sampler2D uHistoryFog;
layout(set = 1, binding = 2) uniform sampler2D uDepth;

layout(push_constant) uniform FogTemporalPC
{
    mat4 prevViewProj;
    vec4 params0; // x=blendAlpha, y=historyValid
} pc;

vec3 reconstructViewPosition(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndc;
    return viewPos.xyz / max(viewPos.w, 0.000001);
}

void main()
{
    vec4 currentFog = texture(uCurrentFog, vUV);
    if (pc.params0.y < 0.5 || pc.params0.x <= 0.0)
    {
        outColor = currentFog;
        return;
    }

    float depth = texture(uDepth, vUV).r;
    if (depth >= 0.9999)
    {
        outColor = currentFog;
        return;
    }

    vec3 viewPos = reconstructViewPosition(vUV, depth);
    vec3 worldPos = (camera.invView * vec4(viewPos, 1.0)).xyz;

    vec4 prevClip = pc.prevViewProj * vec4(worldPos, 1.0);
    if (abs(prevClip.w) <= 0.000001)
    {
        outColor = currentFog;
        return;
    }

    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0)
    {
        outColor = currentFog;
        return;
    }

    vec4 historyFog = texture(uHistoryFog, prevUV);
    vec4 minClamp = currentFog * 0.5 - vec4(0.01);
    vec4 maxClamp = currentFog * 1.5 + vec4(0.01);
    historyFog = clamp(historyFog, minClamp, maxClamp);

    outColor = mix(currentFog, historyFog, clamp(pc.params0.x, 0.0, 0.98));
}
