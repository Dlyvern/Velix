#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSceneColor;
layout(set = 0, binding = 1) uniform sampler2D uFog;

void main()
{
    vec4 sceneColor = texture(uSceneColor, vUV);
    vec4 fog = texture(uFog, vUV);
    vec3 composited = mix(sceneColor.rgb, fog.rgb + sceneColor.rgb, clamp(fog.a, 0.0, 1.0));
    outColor = vec4(composited, sceneColor.a);
}
