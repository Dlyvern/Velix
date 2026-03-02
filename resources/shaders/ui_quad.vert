#version 450

// Vertex2D: vec3 position (xy in NDC), vec2 uv (unused for solid quads)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoords;

layout(push_constant) uniform PC
{
    vec4 color;         // 16 bytes
    vec4 borderColor;   // 16 bytes
    float borderWidth;  // 4 bytes
    float cornerRadius; // 4 bytes (unused for now)
    float pad0;
    float pad1;         // -> 48 bytes total
} pc;

layout(location = 0) out flat vec4  vColor;
layout(location = 1) out flat vec4  vBorderColor;
layout(location = 2) out flat float vBorderWidth;
layout(location = 3) out vec2       vUV;

void main()
{
    gl_Position  = vec4(inPosition.xy, 0.0, 1.0);
    vColor       = pc.color;
    vBorderColor = pc.borderColor;
    vBorderWidth = pc.borderWidth;
    vUV          = inTexCoords;
}
