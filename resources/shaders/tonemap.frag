#version 450

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;
layout(set = 0, binding = 1) uniform sampler2D uLUT;

layout(push_constant) uniform TonemapPC
{
    vec4 tonemapParams; // x=exposure, y=gamma, z=saturation, w=contrast
    vec4 gradeParams;   // x=temperature, y=tint, z=colorGradingEnabled, w=lutEnabled
    vec4 lutParams;     // x=lutStrength
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 acesFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 sampleLUT(vec3 color)
{
    vec2 lutSize = vec2(textureSize(uLUT, 0));
    if (lutSize.x < 2.0 || lutSize.y < 2.0)
        return color;

    float dimension = floor(sqrt(max(lutSize.x, 1.0)) + 0.5);
    if (abs(lutSize.x - (dimension * dimension)) > 1.0 || abs(lutSize.y - dimension) > 1.0)
        return texture(uLUT, color.rg).rgb;

    float maxIndex = max(dimension - 1.0, 1.0);
    vec3 scaled = clamp(color, 0.0, 1.0) * maxIndex;

    float slice0 = floor(scaled.b);
    float slice1 = min(slice0 + 1.0, maxIndex);
    float fracZ = scaled.b - slice0;

    vec2 uv0;
    uv0.x = (scaled.r + slice0 * dimension + 0.5) / lutSize.x;
    uv0.y = (scaled.g + 0.5) / lutSize.y;

    vec2 uv1;
    uv1.x = (scaled.r + slice1 * dimension + 0.5) / lutSize.x;
    uv1.y = uv0.y;

    vec3 c0 = texture(uLUT, uv0).rgb;
    vec3 c1 = texture(uLUT, uv1).rgb;

    return mix(c0, c1, fracZ);
}

void main()
{
    float exposure = pc.tonemapParams.x;
    float gamma = max(pc.tonemapParams.y, 0.0001);
    float saturation = pc.tonemapParams.z;
    float contrast = pc.tonemapParams.w;

    vec3 hdr = texture(uHdrColor, vUV).rgb;
    hdr *= exposure;

    vec3 mapped = acesFilm(hdr);
    mapped = pow(mapped, vec3(1.0 / gamma));

    if (pc.gradeParams.z > 0.5)
    {
        float luma = dot(mapped, vec3(0.2126, 0.7152, 0.0722));
        mapped = mix(vec3(luma), mapped, saturation);

        mapped = clamp((mapped - 0.5) * contrast + 0.5, 0.0, 1.0);

        float temperature = pc.gradeParams.x;
        float tint = pc.gradeParams.y;

        mapped.r = clamp(mapped.r + temperature * 0.1, 0.0, 1.0);
        mapped.b = clamp(mapped.b - temperature * 0.1, 0.0, 1.0);
        mapped.g = clamp(mapped.g + tint * 0.05, 0.0, 1.0);
    }

    if (pc.gradeParams.w > 0.5)
    {
        vec3 lutColor = sampleLUT(mapped);
        mapped = mix(mapped, lutColor, clamp(pc.lutParams.x, 0.0, 1.0));
    }

    outColor = vec4(mapped, 1.0);
}
