#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PC
{
    mat4 worldViewProj;
    mat4 worldToLocal;
    vec4 params;
} pc;

void main()
{
    gl_Position = pc.worldViewProj * vec4(inPosition, 1.0);
}
