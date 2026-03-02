#version 450

layout(location = 0) in vec2      vUV;
layout(location = 1) in flat vec4 vColor;
layout(location = 2) in flat int  vIconType;

layout(location = 0) out vec4 outColor;

// Draw a soft circle icon with an inner symbol indicator.
void main()
{
    vec2  uv   = vUV * 2.0 - 1.0;   // [-1, 1]
    float dist = length(uv);

    if (dist > 1.0)
        discard;

    // Soft outer edge
    float alpha = 1.0 - smoothstep(0.75, 1.0, dist);

    // Outer ring highlight
    float ring = smoothstep(0.6, 0.7, dist) * (1.0 - smoothstep(0.75, 0.85, dist));

    // Inner symbol: simple cross for type 0 (camera), dot for type 1 (light), wave for type 2 (audio)
    float symbol = 0.0;

    if (vIconType == 0) // Camera: cross/viewfinder
    {
        float hBar = 1.0 - smoothstep(0.05, 0.12, abs(uv.y));
        float vBar = 1.0 - smoothstep(0.05, 0.12, abs(uv.x));
        symbol = max(hBar, vBar) * (1.0 - smoothstep(0.45, 0.55, dist));
    }
    else if (vIconType == 1) // Light: sun rays (dot in center)
    {
        symbol = 1.0 - smoothstep(0.15, 0.30, dist);
    }
    else if (vIconType == 2) // Audio: speaker dot
    {
        float wave = sin(uv.x * 12.0) * 0.3;
        symbol = 1.0 - smoothstep(0.04, 0.10, abs(uv.y - wave)) * (1.0 - smoothstep(0.0, 0.55, dist));
        symbol = clamp(symbol, 0.0, 1.0);
    }

    vec3 col = mix(vColor.rgb * 0.6, vColor.rgb + 0.15, symbol);
    col      = mix(col, vec3(1.0), ring * 0.5);

    outColor = vec4(col, alpha * vColor.a);
}
