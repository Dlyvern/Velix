#version 450

struct ParticleData
{
    vec4 positionAndRotation; // xyz = world pos,  w = rotation (radians)
    vec4 color;               // rgba
    vec2 size;                // width, height  (world units)
    uint textureIndex;        // index into the texture array (0 = default white)
    float _pad;
};

layout(set = 0, binding = 0, std430) readonly buffer ParticleBuffer
{
    ParticleData particles[];
};

layout(push_constant) uniform PC
{
    mat4  viewProj;   // 64 bytes
    vec3  right;      // 12 bytes  – camera right vector (world space)
    float _pad0;      //  4 bytes
    vec3  up;         // 12 bytes  – camera up vector (world space)
    float _pad1;      //  4 bytes
} pc;               // 96 bytes total

layout(location = 0) out vec2      vUV;
layout(location = 1) out flat vec4 vColor;
layout(location = 2) out flat uint vTextureIndex;

const vec2 kOffsets[6] = vec2[](
    vec2(-0.5,  0.5),   // top-left
    vec2( 0.5,  0.5),   // top-right
    vec2(-0.5, -0.5),   // bottom-left

    vec2( 0.5,  0.5),   // top-right
    vec2( 0.5, -0.5),   // bottom-right
    vec2(-0.5, -0.5)    // bottom-left
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
