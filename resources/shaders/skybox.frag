#version 450

layout(set = 0, binding = 0) uniform samplerCube skybox;

layout(location = 0) in vec3 fragTextureCoordinates;
layout(location = 0) out vec4 outColor;
layout(location = 1) out uint outObjectId;

void main()
{
    outColor = texture(skybox, fragTextureCoordinates);
    outObjectId = 0;
}