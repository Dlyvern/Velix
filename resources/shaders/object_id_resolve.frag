#version 450

layout(set = 0, binding = 0) uniform usampler2DMS uObjectIdMs;

layout(location = 0) out uint outObjectId;

void main()
{
    ivec2 texSize = textureSize(uObjectIdMs);
    ivec2 pixelCoord = clamp(ivec2(gl_FragCoord.xy), ivec2(0), texSize - ivec2(1));
    outObjectId = texelFetch(uObjectIdMs, pixelCoord, 0).r;
}
