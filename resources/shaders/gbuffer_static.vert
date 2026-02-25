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
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec3 inTangent;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec3 fragNormalView;
layout(location = 2) out vec3 fragPositionView; 
layout(location = 3) out vec3 fragTangentView;
layout(location = 4) out vec3 fragBitangentView;

void main()
{
    fragUV = inTextures;

    vec4 worldPos = modelPushConstant.model * vec4(inPosition, 1.0);
    vec4 viewPos  = cameraUniformObject.view * worldPos;

    mat3 normalMatrixWorld = transpose(inverse(mat3(modelPushConstant.model)));

    vec3 worldNormal    = normalize(normalMatrixWorld * inNormal);
    vec3 worldTangent   = normalize(normalMatrixWorld * inTangent);
    vec3 worldBitangent = normalize(normalMatrixWorld * inBitangent);

    mat3 view3 = mat3(cameraUniformObject.view);

    fragNormalView    = normalize(view3 * worldNormal);
    fragTangentView   = normalize(view3 * worldTangent);
    fragBitangentView = normalize(view3 * worldBitangent);

    fragPositionView = viewPos.xyz;

    gl_Position = cameraUniformObject.projection * viewPos;
}