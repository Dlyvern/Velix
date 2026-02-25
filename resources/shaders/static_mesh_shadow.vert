#version 450

layout(push_constant) uniform LightSpaceMatrixPushConstant
{
    mat4 lightSpaceMatrix;
    mat4 model;
} lightSpaceMatrixPushConstant;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTextures;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec3 inTangent;

void main()
{
    gl_Position = lightSpaceMatrixPushConstant.lightSpaceMatrix * lightSpaceMatrixPushConstant.model * vec4(inPosition, 1.0);
}