#version 450

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;

layout(push_constant) uniform TonemapPC
{
    float exposure;
    float gamma;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 acesFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture(uHdrColor, vUV).rgb;
    hdr *= pc.exposure;

    vec3 mapped = acesFilm(hdr);

    mapped = pow(mapped, vec3(1.0 / pc.gamma));

    outColor = vec4(mapped, 1.0);
}