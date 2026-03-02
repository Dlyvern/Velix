#version 450

layout(location = 0) in flat vec4  vColor;
layout(location = 1) in flat vec4  vBorderColor;
layout(location = 2) in flat float vBorderWidth;
layout(location = 3) in vec2       vUV;

layout(location = 0) out vec4 outColor;

void main()
{
    // UV is [0,1] over the quad.  borderWidth is in UV units.
    float bw = vBorderWidth;
    bool  border = (vUV.x < bw || vUV.x > 1.0 - bw ||
                    vUV.y < bw || vUV.y > 1.0 - bw);

    outColor = border ? vBorderColor : vColor;

    if (outColor.a < 0.01)
        discard;
}
