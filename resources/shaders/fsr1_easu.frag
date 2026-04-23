#version 450






layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform PC {
    vec4 inputSize;
    vec4 outputSize;
} pc;


float rgb2luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }


vec3 fetch(ivec2 p) {

    ivec2 maxP = ivec2(pc.inputSize.xy) - ivec2(1);
    p = clamp(p, ivec2(0), maxP);
    return texelFetch(uInput, p, 0).rgb;
}




float lanczosApprox(float x, float x2) {

    float w = (x2 * 0.25 - 1.0);
    float s = (1.0 - x);
    return clamp(s * w * w, 0.0, 1.0);
}

void main()
{

    vec2 srcPos = vUV * pc.inputSize.xy - 0.5;
    vec2 srcBase = floor(srcPos);
    vec2 f = srcPos - srcBase;
    ivec2 ipos = ivec2(srcBase);







    vec3 b = fetch(ipos + ivec2( 0, -1));
    vec3 c = fetch(ipos + ivec2( 1, -1));
    vec3 d = fetch(ipos + ivec2(-1,  0));
    vec3 e = fetch(ipos + ivec2( 0,  0));
    vec3 f0 = fetch(ipos + ivec2( 1,  0));
    vec3 g = fetch(ipos + ivec2( 2,  0));
    vec3 h = fetch(ipos + ivec2(-1,  1));
    vec3 i = fetch(ipos + ivec2( 0,  1));
    vec3 j = fetch(ipos + ivec2( 1,  1));
    vec3 k = fetch(ipos + ivec2( 2,  1));
    vec3 n = fetch(ipos + ivec2( 0,  2));
    vec3 o = fetch(ipos + ivec2( 1,  2));


    float lE = rgb2luma(e);
    float lF = rgb2luma(f0);
    float lI = rgb2luma(i);
    float lJ = rgb2luma(j);


    float lumaAvg = mix(mix(lE, lF, f.x), mix(lI, lJ, f.x), f.y);


    float lB = rgb2luma(b);
    float lC = rgb2luma(c);
    float lD = rgb2luma(d);
    float lG = rgb2luma(g);
    float lH = rgb2luma(h);
    float lK = rgb2luma(k);
    float lN = rgb2luma(n);
    float lO = rgb2luma(o);

    float dx = (lC + lG + lK + lO) - (lB + lD + lH + lN);
    float dy = (lH + lI + lJ + lK) - (lB + lC + lN + lO);


    float dlen = max(length(vec2(dx, dy)), 1e-4);
    vec2 dir = vec2(dx, dy) / dlen;
    vec2 perp = vec2(-dir.y, dir.x);


    float strength = clamp(dlen * 4.0, 0.0, 1.0);


    float alongLen = 2.0;
    float acrossLen = mix(2.0, 0.5, strength);


    vec3 accumColor = vec3(0.0);
    float accumW = 0.0;


    const vec2 tapOffsets[12] = vec2[12](
        vec2( 0.5, -0.5), vec2( 1.5, -0.5),
        vec2(-0.5,  0.5), vec2( 0.5,  0.5), vec2( 1.5,  0.5), vec2( 2.5,  0.5),
        vec2(-0.5,  1.5), vec2( 0.5,  1.5), vec2( 1.5,  1.5), vec2( 2.5,  1.5),
        vec2( 0.5,  2.5), vec2( 1.5,  2.5)
    );
    vec3 tapColors[12] = vec3[12](b, c, d, e, f0, g, h, i, j, k, n, o);

    for (int t = 0; t < 12; ++t) {
        vec2 off = tapOffsets[t] - (f + vec2(0.5));

        float da = dot(off, dir)  / alongLen;
        float dp = dot(off, perp) / acrossLen;
        float r2 = da*da + dp*dp;
        if (r2 < 1.0) {
            float w = lanczosApprox(sqrt(r2), r2);
            accumColor += tapColors[t] * w;
            accumW += w;
        }
    }


    vec3 result;
    if (accumW > 1e-6) {
        result = accumColor / accumW;
    } else {
        result = mix(mix(e, f0, f.x), mix(i, j, f.x), f.y);
    }


    vec3 bilinear = mix(mix(e, f0, f.x), mix(i, j, f.x), f.y);
    float blend = smoothstep(0.0, 0.05, dlen);
    result = mix(bilinear, result, blend);


    float lmin = min(min(lE, lF), min(lI, lJ));
    float lmax = max(max(lE, lF), max(lI, lJ));
    float lres = rgb2luma(result);
    if (lres < lmin || lres > lmax) {
        result = mix(result, bilinear, 0.5);
    }


    result = max(result, vec3(0.0));

    outColor = vec4(result, 1.0);
}
