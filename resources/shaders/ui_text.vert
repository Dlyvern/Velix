#version 450


layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;

layout(push_constant) uniform PC
{
    vec4 color;
} pc;

layout(location = 0) out vec2      vUV;
layout(location = 1) out flat vec4 vColor;

void main()
{
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
    vUV         = inTexCoords;
    vColor      = pc.color;
}
