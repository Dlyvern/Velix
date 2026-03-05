#version 450

layout(location = 0) in vec2 fragTextureCoordinates;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D uAlbedoTex;

layout(set = 1, binding = 4) uniform MaterialParams
{
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    vec4 uvTransform; // xy scale, zw offset

    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float aoStrength;

    uint flags;
    float alphaCutoff;
    float uvRotation; // degrees
    float ior;
} material;

vec2 getUV()
{
    vec2 uv = fragTextureCoordinates * material.uvTransform.xy;
    float rotationRadians = radians(material.uvRotation);
    float c = cos(rotationRadians);
    float s = sin(rotationRadians);
    mat2 rotation = mat2(c, -s, s, c);
    return (rotation * uv) + material.uvTransform.zw;
}

void main()
{
    vec4 textureSample = texture(uAlbedoTex, getUV());
    vec4 baseColor = textureSample * material.baseColorFactor;
    outColor = vec4(baseColor.rgb, 1.0);
}
