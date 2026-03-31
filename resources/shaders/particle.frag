#version 450
#extension GL_EXT_nonuniform_qualifier : enable

#define MAX_PARTICLE_TEXTURES 8

layout(location = 0) in vec2      vUV;
layout(location = 1) in flat vec4 vColor;
layout(location = 2) in flat uint vTextureIndex;

layout(set = 1, binding = 0) uniform sampler2D particleTextures[MAX_PARTICLE_TEXTURES];
layout(set = 2, binding = 0) uniform sampler2D uSceneDepth;

layout(push_constant) uniform PC
{
    mat4  viewProj;
    vec3  right;
    float softParticleRange;
    vec3  up;
    float softEnabled;
};

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(particleTextures[nonuniformEXT(vTextureIndex)], vUV);
    outColor = texColor * vColor;

    // For untextured particles (slot 0 = default white texture) apply a soft
    // circular shape so they don't render as hard-edged rectangles.
    if (vTextureIndex == 0u)
    {
        vec2  uv    = vUV * 2.0 - 1.0;
        float dist  = length(uv);
        outColor.a *= smoothstep(1.0, 0.75, dist);
    }

    // Soft particles: fade alpha based on depth intersection with scene geometry.
    if (softEnabled > 0.5 && softParticleRange > 0.0)
    {
        ivec2 depthSize = textureSize(uSceneDepth, 0);
        vec2 screenUV = gl_FragCoord.xy / vec2(depthSize);
        float sceneDepth = texture(uSceneDepth, screenUV).r;
        float particleDepth = gl_FragCoord.z;
        float depthDiff = sceneDepth - particleDepth;
        outColor.a *= smoothstep(0.0, softParticleRange, depthDiff);
    }

    if (outColor.a < 0.01)
        discard;
}
