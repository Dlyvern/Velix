#version 450





layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform PC {
    vec4 outputSize;
    vec4 params;
} pc;

vec3 fetchOff(vec2 o) {
    return texture(uInput, vUV + o * pc.outputSize.zw).rgb;
}

void main()
{

    vec3 b = fetchOff(vec2( 0.0, -1.0));
    vec3 d = fetchOff(vec2(-1.0,  0.0));
    vec3 e = fetchOff(vec2( 0.0,  0.0));
    vec3 f = fetchOff(vec2( 1.0,  0.0));
    vec3 h = fetchOff(vec2( 0.0,  1.0));


    vec3 mn = min(min(min(b, d), min(f, h)), e);
    vec3 mx = max(max(max(b, d), max(f, h)), e);


    vec3 mn2 = min(mn, 2.0 - mx);
    vec3 pk = mx - mn2;
    vec3 ampInv = 1.0 / max(pk, vec3(1e-4));




    float sharpness = clamp(pc.params.x, 0.0, 2.0);
    float wMul = exp2(-sharpness);

    vec3 w = -(0.125 * wMul) * ampInv;
    vec3 wSum = 4.0 * w;

    vec3 num = b + d + f + h;
    vec3 result = (num * w + e) / (1.0 + wSum);


    result = clamp(result, mn, mx);

    outColor = vec4(result, 1.0);
}
