#version 450

layout(push_constant) uniform LightSpaceMatrixPushConstant
{
    mat4 lightSpaceMatrix;
    mat4 model;
} lightSpaceMatrixPushConstant;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = lightSpaceMatrixPushConstant.lightSpaceMatrix * lightSpaceMatrixPushConstant.model * vec4(inPosition, 1.0);
}