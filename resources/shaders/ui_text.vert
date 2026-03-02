#version 450

// Vertex2D: vec3 position (xy used as NDC screen pos, z unused) + vec2 uv
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;

layout(push_constant) uniform PC
{
    vec4 color;        // text color + alpha  -> 16 bytes
} pc;

layout(location = 0) out vec2      vUV;
layout(location = 1) out flat vec4 vColor;

void main()
{
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
    vUV         = inTexCoords;
    vColor      = pc.color;
}
