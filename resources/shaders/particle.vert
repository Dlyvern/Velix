#version 450

struct ParticleData
{
    vec4 positionAndRotation;
    vec4 color;
    vec2 size;
    uint textureIndex;
    float _pad;
};

layout(set = 0, binding = 0, std430) readonly buffer ParticleBuffer
{
    ParticleData particles[];
};

layout(push_constant) uniform PC
{
    mat4  viewProj;
    vec3  right;
    float _pad0;
    vec3  up;
    float _pad1;
} pc;

layout(location = 0) out vec2      vUV;
layout(location = 1) out flat vec4 vColor;
layout(location = 2) out flat uint vTextureIndex;

const vec2 kOffsets[6] = vec2[](
    vec2(-0.5,  0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5, -0.5),

    vec2( 0.5,  0.5),
    vec2( 0.5, -0.5),
    vec2(-0.5, -0.5)
);

const vec2 kUVs[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),

    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0)
);

void main()
{
    const int particleIdx = gl_VertexIndex / 6;
    const int vertIdx     = gl_VertexIndex % 6;

    ParticleData p = particles[particleIdx];

    vec3  worldPos = p.positionAndRotation.xyz;
    float rotation = p.positionAndRotation.w;

    float cosR = cos(rotation);
    float sinR = sin(rotation);

    vec2 corner = kOffsets[vertIdx] * p.size;
    vec2 rotated = vec2(
        cosR * corner.x - sinR * corner.y,
        sinR * corner.x + cosR * corner.y
    );

    vec3 billboardPos = worldPos
                      + pc.right * rotated.x
                      + pc.up    * rotated.y;

    gl_Position    = pc.viewProj * vec4(billboardPos, 1.0);
    vUV            = kUVs[vertIdx];
    vColor         = p.color;
    vTextureIndex  = p.textureIndex;
}
