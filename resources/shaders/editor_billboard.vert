#version 450


layout(push_constant) uniform PC
{
    mat4  viewProj;
    vec3  right;
    float size;
    vec3  up;
    float pad0;
    vec3  worldPos;
    int   iconType;
    vec4  color;
} pc;

layout(location = 0) out vec2 vUV;
layout(location = 1) out flat vec4 vColor;
layout(location = 2) out flat int  vIconType;

void main()
{
    const vec2 offsets[6] = vec2[](
        vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(-0.5,  0.5),
        vec2( 0.5, -0.5), vec2(0.5,  0.5), vec2(-0.5,  0.5)
    );
    const vec2 uvs[6] = vec2[](
        vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0),
        vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0)
    );

    vec2  corner   = offsets[gl_VertexIndex] * pc.size;
    vec3  worldPos = pc.worldPos + pc.right * corner.x + pc.up * corner.y;

    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    vUV         = uvs[gl_VertexIndex];
    vColor      = pc.color;
    vIconType   = pc.iconType;
}
