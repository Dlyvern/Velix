#version 450

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;

layout(push_constant) uniform TonemapPC
{
    float exposure;
    float gamma;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

void main()
{
    vec3 hdr = texture(uHdrColor, vUV).rgb;
    hdr *= pc.exposure;

    vec3 mapped = hdr / (hdr + vec3(1.0));
    outColor = vec4(mapped, 1.0);
}