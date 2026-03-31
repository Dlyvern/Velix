#version 450

layout(set = 0, binding = 0) uniform CameraUniformObject
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} cameraUniformObject;

layout(location = 0) in vec2 fragTextureCoordinates;
layout(location = 1) in vec3 fragWorldNormal;
layout(location = 2) in vec3 fragWorldPosition;

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

    vec3 normal = normalize(fragWorldNormal);
    vec3 lightDir = normalize(vec3(0.45, 0.8, 0.35));
    vec3 viewPos = cameraUniformObject.invView[3].xyz;
    vec3 viewDir = normalize(viewPos - fragWorldPosition);
    vec3 halfVector = normalize(lightDir + viewDir);

    float ndl = max(dot(normal, lightDir), 0.0);
    float hemi = normal.y * 0.5 + 0.5;
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), 2.0);
    float specular = pow(max(dot(normal, halfVector), 0.0), 32.0);

    vec3 lighting =
        vec3(0.18) +
        vec3(0.18, 0.2, 0.24) * hemi +
        vec3(0.72, 0.7, 0.66) * ndl +
        vec3(0.1, 0.12, 0.16) * rim +
        vec3(0.18) * specular;

    outColor = vec4(baseColor.rgb * lighting, 1.0);
}
