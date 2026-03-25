#version 450

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uColor;
layout(set = 0, binding = 1) uniform sampler2D uDepth;

layout(push_constant) uniform PC
{
    mat4  invViewProj;   // inverse(proj * view) for this frame
    mat4  prevViewProj;  // proj * view from last frame
    vec2  texelSize;
    float intensity;
    float numSamplesF;   // int cast in shader
} pc;

const float kEpsilon = 1e-5;

void main()
{
    vec3 color = texture(uColor, vUV).rgb;

    float depth = texture(uDepth, vUV).r;
    if (depth >= 1.0)
    {
        outColor = vec4(color, 1.0);
        return;
    }

    // Reconstruct world-space position from NDC + depth
    vec4 ndcPos  = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldH  = pc.invViewProj * ndcPos;
    vec3 worldPos = worldH.xyz / max(worldH.w, kEpsilon);

    // Project world position with previous frame VP
    vec4 prevClip = pc.prevViewProj * vec4(worldPos, 1.0);
    vec2 prevNDC  = prevClip.xy / max(prevClip.w, kEpsilon);
    vec2 prevUV   = prevNDC * 0.5 + 0.5;

    // Screen-space velocity vector (current → previous)
    vec2 velocity = (prevUV - vUV) * pc.intensity;

    // Clamp velocity to a maximum radius to avoid extreme blur
    float maxRadius = 0.05;
    float len = length(velocity);
    if (len > maxRadius)
        velocity = velocity / len * maxRadius;

    // Accumulate samples along velocity
    int numSamples = max(int(pc.numSamplesF + 0.5), 1);
    vec3 accumulated = color;
    float totalWeight = 1.0;

    for (int i = 1; i < numSamples; ++i)
    {
        float t = float(i) / float(numSamples - 1) - 0.5; // [-0.5, +0.5]
        vec2 sampleUV = vUV + velocity * t;

        // Skip out-of-bounds samples
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float w = 1.0 - abs(t) * 0.5; // weight center samples more
        accumulated += texture(uColor, sampleUV).rgb * w;
        totalWeight += w;
    }

    outColor = vec4(accumulated / totalWeight, 1.0);
}
