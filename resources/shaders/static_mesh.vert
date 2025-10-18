#version 450

layout(push_constant) uniform ModelPushConstant
{
    mat4 model;
} modelPushConstant;

layout(set = 0, binding = 0) uniform CameraUniformObject
{
    mat4 view;
    mat4 projection;
} cameraUniformObject;

layout(set = 0, binding = 1) uniform LightSpaceMatrixUniformObject
{
    mat4 lightSpaceMatrix;
} lightSpaceMatrixUniformObject;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTextures;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTextureCoordinates;
layout(location = 1) out vec3 fragNormalView;
layout(location = 2) out vec3 fragPositionView;
layout(location = 3) out vec4 fragPositionLightSpace;

void main() 
{
    mat4 model = modelPushConstant.model;
    mat4 view = cameraUniformObject.view;
    mat4 modelView = view * model;

    vec4 positionView = view * model * vec4(inPosition, 1.0);
    fragPositionView = positionView.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    fragNormalView = normalize(normalMatrix * inNormal);

    fragPositionLightSpace = lightSpaceMatrixUniformObject.lightSpaceMatrix * modelPushConstant.model * vec4(inPosition, 1.0);

    gl_Position = cameraUniformObject.projection * positionView;
    
    fragTextureCoordinates = inTextures;
}