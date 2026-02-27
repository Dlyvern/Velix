#version 450

// FXAA 3.11 Quality implementation
// Based on Timothy Lottes' FXAA algorithm (NVIDIA)

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;

layout(push_constant) uniform FXAAPC
{
    vec2  texelSize; // 1.0 / resolution
    float enabled;
    float subpixelQuality; // 0.75 is a good default
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

#define FXAA_SPAN_MAX        8.0
#define FXAA_REDUCE_MUL      (1.0 / 8.0)
#define FXAA_REDUCE_MIN      (1.0 / 128.0)
#define FXAA_EDGE_THRESHOLD_MIN  0.0312
#define FXAA_EDGE_THRESHOLD      0.125

float luma(vec3 rgb)
{
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main()
{
    if (pc.enabled < 0.5)
    {
        outColor = texture(uSceneColor, vUV);
        return;
    }

    vec2 uv  = vUV;
    vec2 ts  = pc.texelSize;

    // Sample corners + center
    vec3 rgbNW = texture(uSceneColor, uv + vec2(-1.0, -1.0) * ts).rgb;
    vec3 rgbNE = texture(uSceneColor, uv + vec2( 1.0, -1.0) * ts).rgb;
    vec3 rgbSW = texture(uSceneColor, uv + vec2(-1.0,  1.0) * ts).rgb;
    vec3 rgbSE = texture(uSceneColor, uv + vec2( 1.0,  1.0) * ts).rgb;
    vec3 rgbM  = texture(uSceneColor, uv).rgb;

    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);
    float lumaM  = luma(rgbM);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // Early-out on flat regions
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD))
    {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // Compute blend direction
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * ts;

    // Two sample points along the edge
    vec3 rgbA = 0.5 * (
        texture(uSceneColor, uv + dir * (1.0/3.0 - 0.5)).rgb +
        texture(uSceneColor, uv + dir * (2.0/3.0 - 0.5)).rgb);

    vec3 rgbB = 0.5 * rgbA + 0.25 * (
        texture(uSceneColor, uv + dir * -0.5).rgb +
        texture(uSceneColor, uv + dir *  0.5).rgb);

    float lumaB = luma(rgbB);
    vec3  result = ((lumaB < lumaMin) || (lumaB > lumaMax)) ? rgbA : rgbB;

    outColor = vec4(result, 1.0);
}
