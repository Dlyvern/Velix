#version 450











layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform PC
{
    vec2  texelSize;
    float enabled;
    float edgeThreshold;
    float maxSearchSteps;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;





float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }


float lumaTap(vec2 uv, vec2 offset)
{
    return luma(texture(uInput, uv + offset * pc.texelSize).rgb);
}





void main()
{
    vec4 center = texture(uInput, vUV);

    if (pc.enabled < 0.5)
    {
        outColor = center;
        return;
    }

    float lumaC = luma(center.rgb);


    float lumaN  = lumaTap(vUV, vec2( 0.0,  1.0));
    float lumaS  = lumaTap(vUV, vec2( 0.0, -1.0));
    float lumaE  = lumaTap(vUV, vec2( 1.0,  0.0));
    float lumaW  = lumaTap(vUV, vec2(-1.0,  0.0));

    float maxLuma  = max(lumaC, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float minLuma  = min(lumaC, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaRange = maxLuma - minLuma;


    if (lumaRange < max(pc.edgeThreshold, maxLuma * 0.125))
    {
        outColor = center;
        return;
    }


    float lumaNW = lumaTap(vUV, vec2(-1.0,  1.0));
    float lumaNE = lumaTap(vUV, vec2( 1.0,  1.0));
    float lumaSW = lumaTap(vUV, vec2(-1.0, -1.0));
    float lumaSE = lumaTap(vUV, vec2( 1.0, -1.0));


    float edgeH = abs(lumaN + lumaS - 2.0 * lumaC) * 2.0
                + abs(lumaNE + lumaSE - 2.0 * lumaE)
                + abs(lumaNW + lumaSW - 2.0 * lumaW);

    float edgeV = abs(lumaE + lumaW - 2.0 * lumaC) * 2.0
                + abs(lumaNE + lumaNW - 2.0 * lumaN)
                + abs(lumaSE + lumaSW - 2.0 * lumaS);

    bool isHorizontal = edgeH >= edgeV;


    float luma1 = isHorizontal ? lumaN  : lumaE;
    float luma2 = isHorizontal ? lumaS  : lumaW;
    float grad1 = abs(luma1 - lumaC);
    float grad2 = abs(luma2 - lumaC);


    vec2 stepDir = isHorizontal ? vec2(0.0, pc.texelSize.y) : vec2(pc.texelSize.x, 0.0);
    if (grad1 < grad2)
        stepDir = -stepDir;


    vec2 uv1 = vUV + stepDir;
    vec4 neighbour = texture(uInput, uv1);


    float blendFactor = lumaRange / (maxLuma + 0.0001);
    blendFactor = smoothstep(0.0, 1.0, blendFactor);
    blendFactor = min(blendFactor, 0.5);


    outColor = mix(center, neighbour, blendFactor);
}
