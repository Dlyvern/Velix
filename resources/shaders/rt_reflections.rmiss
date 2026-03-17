#version 460
#extension GL_EXT_ray_tracing : require

struct ReflectionPayload
{
    vec3 radiance;
    float hitT;
    uint hit;
};

layout(location = 0) rayPayloadInEXT ReflectionPayload payload;

void main()
{
    payload.hit = 0u;
    payload.radiance = vec3(0.0);
    payload.hitT = 0.0;
}
