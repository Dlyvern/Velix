#version 450

layout(location = 0) in vec2      vUV;
layout(location = 1) in flat vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor;

    // ── Soft circular shape (optional — comment out for rectangular quads) ───
    // Maps UV [0,1] → centred [-1,1], then radial fade
    vec2  uv    = vUV * 2.0 - 1.0;
    float dist  = length(uv);
    float alpha = smoothstep(1.0, 0.75, dist);
    outColor.a *= alpha;

    if (outColor.a < 0.01)
        discard;
}
