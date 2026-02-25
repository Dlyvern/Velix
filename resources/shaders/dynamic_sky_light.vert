#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 outWorldPos;

layout(push_constant) uniform PushConstant 
{
    mat4 view;
    mat4 projection;
} pc;

void main()
{
    outWorldPos = inPosition;
    vec4 clipPos = pc.projection * pc.view * vec4(inPosition, 1.0);
    // Force sky depth to far plane so it only renders where scene depth remains at 1.0.
    gl_Position = clipPos.xyww;
}
