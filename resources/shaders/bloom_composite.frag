#version 450

// Bloom composite: upsamples the half-res bloom texture using a 9-tap
// bilinear tent filter and additively blends it onto the LDR scene.

layout(set = 0, binding = 0) uniform sampler2D uSceneLDR;
layout(set = 0, binding = 1) uniform sampler2D uBloom;     // half-res bloom texture

layout(push_constant) uniform BloomCompositePC
{
    vec2  texelSize;   // 1.0 / full-res resolution (bloom texel size is 2x this)
    float strength;
    float enabled;
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// 9-tap bilinear tent filter to upsample bloom from half-res
vec3 upsampleBloom(vec2 uv)
{
    vec2 ts = pc.texelSize * 2.0; // bloom is half-res, so double texel size

    vec3 c  = vec3(0.0);
    c += texture(uBloom, uv + vec2(-1.0, -1.0) * ts).rgb * (1.0/16.0);
    c += texture(uBloom, uv + vec2( 0.0, -1.0) * ts).rgb * (2.0/16.0);
    c += texture(uBloom, uv + vec2( 1.0, -1.0) * ts).rgb * (1.0/16.0);
    c += texture(uBloom, uv + vec2(-1.0,  0.0) * ts).rgb * (2.0/16.0);
    c += texture(uBloom, uv + vec2( 0.0,  0.0) * ts).rgb * (4.0/16.0);
    c += texture(uBloom, uv + vec2( 1.0,  0.0) * ts).rgb * (2.0/16.0);
    c += texture(uBloom, uv + vec2(-1.0,  1.0) * ts).rgb * (1.0/16.0);
    c += texture(uBloom, uv + vec2( 0.0,  1.0) * ts).rgb * (2.0/16.0);
    c += texture(uBloom, uv + vec2( 1.0,  1.0) * ts).rgb * (1.0/16.0);
    return c;
}

void main()
{
    vec3 scene = texture(uSceneLDR, vUV).rgb;

    if (pc.enabled < 0.5)
    {
        outColor = vec4(scene, 1.0);
        return;
    }

    vec3 bloom = upsampleBloom(vUV);
    outColor   = vec4(scene + bloom * pc.strength, 1.0);
}
