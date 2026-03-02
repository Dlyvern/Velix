#version 450

// Cinematic Effects: Vignette + Film Grain + Chromatic Aberration
// Single fullscreen pass operating on LDR input (post-AA).
//
// set 0  = input color (combined image sampler, binding 0)

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform CinematicPC
{
    float vignetteStrength;      // [0, 1]
    float grainStrength;         // [0, 0.2]
    float aberrationStrength;    // [0, 0.02]
    float time;                  // animated grain seed
    float vignetteEnabled;
    float grainEnabled;
    float aberrationEnabled;
    float _pad;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// Simple hash for film grain noise
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main()
{
    vec3 color;

    if (pc.aberrationEnabled > 0.5)
    {
        // Chromatic aberration: offset R and B channels radially from screen center
        vec2 offset    = (vUV - 0.5) * pc.aberrationStrength;
        float r        = texture(uInput, vUV + offset).r;
        float g        = texture(uInput, vUV).g;
        float b        = texture(uInput, vUV - offset).b;
        color          = vec3(r, g, b);
    }
    else
    {
        color = texture(uInput, vUV).rgb;
    }

    if (pc.vignetteEnabled > 0.5)
    {
        float dist     = length(vUV - 0.5) * 2.0; // 0 at center, ~1.41 at corner
        float vignette = 1.0 - smoothstep(0.5, 1.4, dist) * pc.vignetteStrength;
        color         *= vignette;
    }

    if (pc.grainEnabled > 0.5)
    {
        float noise  = hash(vUV + fract(pc.time * 0.07317)) * 2.0 - 1.0;
        color       += noise * pc.grainStrength;
    }

    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
