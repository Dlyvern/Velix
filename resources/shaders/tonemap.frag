#version 450

layout(set = 0, binding = 0) uniform sampler2D uHdrColor;

layout(push_constant) uniform TonemapPC
{
    vec4 tonemapParams; // x=exposure, y=gamma, z=saturation, w=contrast
    vec4 gradeParams;   // x=temperature, y=tint, z=colorGradingEnabled, w=unused
} pc;

layout(location = 0) in vec2 vUV;
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

void main()
{
    float exposure = pc.tonemapParams.x;
    float gamma = max(pc.tonemapParams.y, 0.0001);
    float saturation = pc.tonemapParams.z;
    float contrast = pc.tonemapParams.w;

    vec3 hdr = texture(uHdrColor, vUV).rgb * exposure;
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

    outColor = vec4(mapped, 1.0);
    // outColor.rgb *= vec3(1.0, 0.3, 0.3);  
}
