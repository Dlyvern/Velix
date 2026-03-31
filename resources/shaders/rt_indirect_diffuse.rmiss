#version 460
#extension GL_EXT_ray_tracing : require

struct GIPayload
{
    vec3  radiance;
    uint  hit;
};

layout(location = 0) rayPayloadInEXT GIPayload payload;

void main()
{
    // Ray missed all geometry — rgen will use sky color for this direction.
    payload.hit      = 0u;
    payload.radiance = vec3(0.0);
}
