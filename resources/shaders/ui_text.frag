#version 450

// Single-channel (R8) font atlas
layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) in vec2      vUV;
layout(location = 1) in flat vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC
{
    vec4 color;
} pc;

void main()
{
    float coverage = texture(uAtlas, vUV).r;
    outColor = vec4(pc.color.rgb, pc.color.a * coverage);

    if (outColor.a < 0.01)
        discard;
}
