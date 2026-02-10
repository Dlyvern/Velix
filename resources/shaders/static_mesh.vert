#version 450

layout(push_constant) uniform ModelPushConstant
{
    mat4 model;
    uint objectId;
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
    fragTextureCoordinates = inTextures;

    vec4 worldPos = modelPushConstant.model * vec4(inPosition, 1.0);
    vec4 viewPos = cameraUniformObject.view * worldPos;
    mat3 normalMatrix = transpose(inverse(mat3(modelPushConstant.model)));
    vec3 worldNormal = normalize(normalMatrix * inNormal);

    fragPositionView = viewPos.xyz;
    fragNormalView = mat3(cameraUniformObject.view) * worldNormal;

    fragPositionLightSpace = lightSpaceMatrixUniformObject.lightSpaceMatrix * worldPos;

    gl_Position = cameraUniformObject.projection * viewPos;
}