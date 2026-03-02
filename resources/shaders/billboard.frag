#version 450

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(location = 0) in vec2      vUV;
layout(location = 1) in flat vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(uTexture, vUV);
    outColor = texColor * vColor;

    if (outColor.a < 0.01)
        discard;
}
