#version 450

layout(push_constant) uniform PushConstant
{
    mat4 view;
    mat4 projection;
} pc;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 fragTextureCoordinates;

void main()
{
    fragTextureCoordinates = inPosition;

    vec4 pos = pc.projection * pc.view * vec4(inPosition, 1.0);

    gl_Position = pos.xyww;
}