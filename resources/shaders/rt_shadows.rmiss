#version 460
#extension GL_EXT_ray_tracing : require

struct ShadowPayload
{
    uint occluded;
};

layout(location = 0) rayPayloadInEXT ShadowPayload payload;

void main()
{
    payload.occluded = 0u;
}
