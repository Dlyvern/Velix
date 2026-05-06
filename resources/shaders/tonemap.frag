#version 450

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;

layout(push_constant) uniform TonemapPC
{
    vec4 tonemapParams;
    vec4 gradeParams;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

const mat3 AGX_INPUT = mat3(
    0.842479062253094, 0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772,  0.0784336,
    0.0792237451477643, 0.0791661274605434, 0.879142973793104);

const mat3 AGX_OUTPUT = mat3(
     1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
    -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
    -0.0990297440797205, -0.0989611768448433,  1.15107367264116);

vec3 agxContrastSigmoid(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return  15.5     * x4 * x2
          - 40.14    * x4 * x
          + 31.96    * x4
          -  6.868   * x2 * x
          +  0.4298  * x2
          +  0.1191  * x
          -  0.00232;
}

vec3 agxTonemap(vec3 linearColor)
{
    const float minEv = -12.47393;
    const float maxEv =   4.026069;

    vec3 v = AGX_INPUT * max(linearColor, vec3(0.0));
    v = clamp(log2(max(v, vec3(1e-10))), vec3(minEv), vec3(maxEv));
    v = (v - minEv) / (maxEv - minEv);
    v = agxContrastSigmoid(v);
    v = AGX_OUTPUT * v;
    return clamp(v, vec3(0.0), vec3(1.0));
}

vec3 agxPunchyLook(vec3 v)
{
    const vec3 luma_w = vec3(0.2126, 0.7152, 0.0722);
    float luma = dot(v, luma_w);
    v = pow(v, vec3(1.35));
    return luma + 1.4 * (v - luma);
}

void main()
{
    float exposure   = pc.tonemapParams.x;
    float gamma      = max(pc.tonemapParams.y, 0.0001);
    float saturation = pc.tonemapParams.z;
    float contrast   = pc.tonemapParams.w;

    vec3 hdr = texture(uHdrColor, vUV).rgb * exposure;

    vec3 mapped = agxTonemap(hdr);
    mapped = agxPunchyLook(mapped);

    mapped = pow(max(mapped, vec3(0.0)), vec3(1.0 / gamma));

    if (pc.gradeParams.z > 0.5)
    {
        float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
        mapped = mix(vec3(luma), mapped, saturation);
        mapped = clamp((mapped - 0.5) * contrast + 0.5, 0.0, 1.0);

        float temperature = pc.gradeParams.x;
        float tint        = pc.gradeParams.y;

        mapped.r = clamp(mapped.r + temperature * 0.1, 0.0, 1.0);
        mapped.b = clamp(mapped.b - temperature * 0.1, 0.0, 1.0);
        mapped.g = clamp(mapped.g + tint        * 0.05, 0.0, 1.0);
    }

    outColor = vec4(mapped, 1.0);
}
