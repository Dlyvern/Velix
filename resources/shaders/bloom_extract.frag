#version 450

// Bloom bright-region extraction with 13-tap tent filter (Kawase-style)
// Reads from HDR scene color, outputs bright regions at half resolution.

layout(set = 0, binding = 0) uniform sampler2D uHDRColor;

layout(push_constant) uniform BloomExtractPC
{
    vec2  texelSize;   // 1.0 / full-res resolution
    float threshold;   // brightness threshold
    float knee;        // soft knee width
    float enabled;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outBloom;

float luma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Soft-threshold curve from Kawase/Karis
vec3 softThreshold(vec3 color)
{
    float brightness = max(max(color.r, color.g), color.b);
    float rq = clamp(brightness - pc.threshold + pc.knee, 0.0, 2.0 * pc.knee);
    rq = (rq * rq) / max(4.0 * pc.knee, 0.00001);
    float weight = max(rq, brightness - pc.threshold) / max(brightness, 0.00001);
    return color * weight;
}

void main()
{
    if (pc.enabled < 0.5)
    {
        outBloom = vec4(0.0);
        return;
    }

    vec2 ts = pc.texelSize;

    // 13-tap downsampling filter (Karis average / COD dual Kawase inspired)
    vec3 a = texture(uHDRColor, vUV + vec2(-2.0, -2.0) * ts).rgb;
    vec3 b = texture(uHDRColor, vUV + vec2( 0.0, -2.0) * ts).rgb;
    vec3 c = texture(uHDRColor, vUV + vec2( 2.0, -2.0) * ts).rgb;

    vec3 d = texture(uHDRColor, vUV + vec2(-2.0,  0.0) * ts).rgb;
    vec3 e = texture(uHDRColor, vUV + vec2( 0.0,  0.0) * ts).rgb;
    vec3 f = texture(uHDRColor, vUV + vec2( 2.0,  0.0) * ts).rgb;

    vec3 g = texture(uHDRColor, vUV + vec2(-2.0,  2.0) * ts).rgb;
    vec3 h = texture(uHDRColor, vUV + vec2( 0.0,  2.0) * ts).rgb;
    vec3 i = texture(uHDRColor, vUV + vec2( 2.0,  2.0) * ts).rgb;

    vec3 j = texture(uHDRColor, vUV + vec2(-1.0, -1.0) * ts).rgb;
    vec3 k = texture(uHDRColor, vUV + vec2( 1.0, -1.0) * ts).rgb;
    vec3 l = texture(uHDRColor, vUV + vec2(-1.0,  1.0) * ts).rgb;
    vec3 m = texture(uHDRColor, vUV + vec2( 1.0,  1.0) * ts).rgb;

    // Weighted sum (centre 2x2 group weighted higher)
    vec3 col = e * 0.125;
    col += (a + c + g + i) * 0.03125;
    col += (b + d + f + h) * 0.0625;
    col += (j + k + l + m) * 0.125;

    outBloom = vec4(softThreshold(col), 1.0);
}
