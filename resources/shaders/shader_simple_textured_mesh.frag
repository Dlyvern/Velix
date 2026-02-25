#version 450

layout(location = 0) in vec2 fragTextureCoordinates;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D uAlbedoTex;

layout(set = 1, binding = 4) uniform MaterialParams
{
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    vec4 uvTransform;

    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float aoStrength;

    uint flags;
    float alphaCutoff;
    vec2 _padding;
} material;

void main()
{
    vec4 textureSample = texture(uAlbedoTex, fragTextureCoordinates);
    vec4 baseColor = textureSample * material.baseColorFactor;
    outColor = vec4(baseColor.rgb, 1.0);
}
