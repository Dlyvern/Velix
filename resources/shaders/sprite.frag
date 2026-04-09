#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 1, binding = 0) uniform sampler2D uTextures[8];

layout(location = 0) in  vec2  inUV;
layout(location = 1) in  vec4  inColor;
layout(location = 2) flat in uint inTextureIndex;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texColor = texture(uTextures[nonuniformEXT(inTextureIndex)], inUV);
    outColor = texColor * inColor;

    if (outColor.a < 0.01)
        discard;
}
