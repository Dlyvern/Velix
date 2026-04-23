#version 450

struct SpriteGPUData
{
    vec4 positionAndRotation;
    vec2 size;
    float _pad0;
    float _pad1;
    vec4 color;
    vec4 uvRect;
    uint textureIndex;
    uint flipX;
    uint flipY;
    uint _pad2;
};

layout(set = 0, binding = 0) readonly buffer SpriteSSBO
{
    SpriteGPUData sprites[];
};

layout(push_constant) uniform PC
{
    mat4 viewProj;
    vec3 right;
    float _pad0;
    vec3 up;
    float _pad1;
};

layout(location = 0) out vec2  outUV;
layout(location = 1) out vec4  outColor;
layout(location = 2) flat out uint outTextureIndex;


const vec2 quadOffsets[6] = vec2[](
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2( 0.5,  0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5),
    vec2(-0.5, -0.5)
);

void main()
{
    uint spriteIdx = uint(gl_VertexIndex) / 6u;
    uint vertIdx   = uint(gl_VertexIndex) % 6u;

    SpriteGPUData s = sprites[spriteIdx];
    vec2 local = quadOffsets[vertIdx];


    float cosR = cos(s.positionAndRotation.w);
    float sinR = sin(s.positionAndRotation.w);
    vec2 rotated = vec2(
        cosR * local.x - sinR * local.y,
        sinR * local.x + cosR * local.y
    );


    vec3 worldPos = s.positionAndRotation.xyz
                  + right * rotated.x * s.size.x
                  + up    * rotated.y * s.size.y;

    gl_Position = viewProj * vec4(worldPos, 1.0);


    float u = local.x + 0.5;
    float v = 0.5 - local.y;

    if (s.flipX != 0u) u = 1.0 - u;
    if (s.flipY != 0u) v = 1.0 - v;

    outUV           = vec2(mix(s.uvRect.x, s.uvRect.z, u),
                           mix(s.uvRect.y, s.uvRect.w, v));
    outColor        = s.color;
    outTextureIndex = s.textureIndex;
}
