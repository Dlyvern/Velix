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

const uint MATERIAL_FLAG_FLIP_V = 1u << 4;
const uint MATERIAL_FLAG_FLIP_U = 1u << 5;
const uint MATERIAL_FLAG_CLAMP_UV = 1u << 6;

vec2 getUV()
{
    vec2 uv = fragTextureCoordinates;
    if ((material.flags & MATERIAL_FLAG_FLIP_U) != 0u)
        uv.x = 1.0 - uv.x;
    if ((material.flags & MATERIAL_FLAG_FLIP_V) != 0u)
        uv.y = 1.0 - uv.y;

    uv *= material.uvTransform.xy;
    float rotationRadians = radians(material.uvRotation);
    float c = cos(rotationRadians);
    float s = sin(rotationRadians);
    mat2 rotation = mat2(c, -s, s, c);
    uv = (rotation * uv) + material.uvTransform.zw;

    if ((material.flags & MATERIAL_FLAG_CLAMP_UV) != 0u)
        uv = clamp(uv, vec2(0.0), vec2(1.0));

    return uv;
}

void main()
{
    vec4 textureSample = texture(uAlbedoTex, getUV());
    vec4 baseColor = textureSample * material.baseColorFactor;
    outColor = vec4(baseColor.rgb, 1.0);
}
