#version 460
#extension GL_EXT_ray_tracing : require

struct ShadowPayload
{
    uint occluded;
};

layout(location = 0) rayPayloadInEXT ShadowPayload payload;
hitAttributeEXT vec2 hitAttributes;

void main()
{
    payload.occluded = 1u;
}
