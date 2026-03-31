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
    mat4 invView;
    mat4 invProjection;
} cameraUniformObject;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTextures;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 fragTextureCoordinates;
layout(location = 1) out vec3 fragWorldNormal;
layout(location = 2) out vec3 fragWorldPosition;

void main() 
{
    fragTextureCoordinates = inTextures;

    vec4 worldPos = modelPushConstant.model * vec4(inPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(modelPushConstant.model)));
    fragWorldNormal = normalize(normalMatrix * inNormal);
    fragWorldPosition = worldPos.xyz;
    vec4 viewPos = cameraUniformObject.view * worldPos;

    gl_Position = cameraUniformObject.projection * viewPos;
}
