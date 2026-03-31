#version 460
#extension GL_EXT_ray_tracing         : require
#extension GL_EXT_buffer_reference2   : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64    : require
#extension GL_EXT_nonuniform_qualifier: require

const int   DIRECTIONAL_LIGHT_TYPE = 0;
const int   SPOT_LIGHT_TYPE        = 1;
const int   POINT_LIGHT_TYPE       = 2;
const int   MAX_LIGHT_COUNT        = 16;
const float PI                     = 3.14159265359;

struct Light
{
    vec4 position;
    vec4 direction;
    vec4 colorStrength;
    vec4 parameters;
    vec4 shadowInfo;
};

struct MaterialParams
{
    vec4  baseColorFactor;
    vec4  emissiveFactor;
    vec4  uvTransform;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float aoStrength;
    uint  flags;
    float alphaCutoff;
    float uvRotation;
    float ior;
    uint  albedoTexIdx;
    uint  normalTexIdx;
    uint  ormTexIdx;
    uint  emissiveTexIdx;
};

struct GIInstance
{
    uint64_t     vertexAddress;
    uint64_t     indexAddress;
    uint         vertexStride;
    uint         padding0;
    uint         padding1;
    uint         padding2;
    MaterialParams material;
};

struct GIPayload
{
    vec3 radiance;
    uint hit;
};

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

layout(std430, set = 0, binding = 2) readonly buffer LightSSBO
{
    int   lightCount;
    Light lights[];
} lightData;

layout(std430, set = 1, binding = 4) readonly buffer GIInstanceSSBO
{
    GIInstance instances[];
} instanceData;

layout(set = 2, binding = 0) uniform sampler2D allTextures[];

// Shared with rgen — sunHeight drives the sky ambient term at secondary hits.
layout(push_constant) uniform GIPC
{
    int   giSamples;
    float sunHeight;
    float frameOffset;
    float pad;
} pc;

layout(location = 0) rayPayloadInEXT GIPayload payload;
hitAttributeEXT vec2 hitBarycentrics;

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer IndexRef  { uint  index; };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer VertexRef
{
    vec3 position;
    vec2 uv;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
};

uint      loadIndex (uint64_t base, uint i)              { return IndexRef (base + uint64_t(i) * 4ul).index; }
VertexRef loadVertex(uint64_t base, uint stride, uint i) { return VertexRef(base + uint64_t(i) * uint64_t(stride)); }

void main()
{
    payload.hit = 1u;

    GIInstance instance = instanceData.instances[gl_InstanceCustomIndexEXT];
    if (instance.vertexAddress == 0ul || instance.indexAddress == 0ul || instance.vertexStride == 0u)
    {
        payload.radiance = vec3(0.0);
        return;
    }

    uint baseIndex = gl_PrimitiveID * 3u;
    uint i0 = loadIndex(instance.indexAddress, baseIndex + 0u);
    uint i1 = loadIndex(instance.indexAddress, baseIndex + 1u);
    uint i2 = loadIndex(instance.indexAddress, baseIndex + 2u);

    VertexRef v0 = loadVertex(instance.vertexAddress, instance.vertexStride, i0);
    VertexRef v1 = loadVertex(instance.vertexAddress, instance.vertexStride, i1);
    VertexRef v2 = loadVertex(instance.vertexAddress, instance.vertexStride, i2);

    vec3 bary = vec3(1.0 - hitBarycentrics.x - hitBarycentrics.y, hitBarycentrics.x, hitBarycentrics.y);

    MaterialParams mat = instance.material;

    vec2 uv0 = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
    vec2 uv  = uv0 * mat.uvTransform.xy;
    float rotRad = radians(mat.uvRotation);
    float c = cos(rotRad), s = sin(rotRad);
    uv = mat2(c, -s, s, c) * uv + mat.uvTransform.zw;

    vec3 objectNormal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    mat3 worldToObject = mat3(gl_WorldToObjectEXT[0], gl_WorldToObjectEXT[1], gl_WorldToObjectEXT[2]);
    vec3 N_world = normalize(transpose(worldToObject) * objectNormal);
    vec3 V_world = normalize(-gl_WorldRayDirectionEXT);
    if (dot(N_world, V_world) < 0.0) N_world = -N_world;

    vec3 hitPosWorld = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 hitPosView  = (camera.view * vec4(hitPosWorld, 1.0)).xyz;
    vec3 N_view      = normalize((camera.view * vec4(N_world, 0.0)).xyz);

    // Sample albedo
    vec3 albedo = mat.baseColorFactor.rgb;
    if (mat.albedoTexIdx > 0u)
        albedo *= texture(allTextures[nonuniformEXT(mat.albedoTexIdx)], uv).rgb;

    // Emissive surfaces contribute directly (area lights / glowing objects)
    vec3 emissive = mat.emissiveFactor.rgb;
    if (mat.emissiveTexIdx > 0u)
        emissive *= texture(allTextures[nonuniformEXT(mat.emissiveTexIdx)], uv).rgb;
    if (any(greaterThan(emissive, vec3(0.001))))
    {
        payload.radiance = emissive;
        return;
    }

    // Lambertian direct lighting at secondary hit point
    vec3 Lo = vec3(0.0);
    int count = min(lightData.lightCount, MAX_LIGHT_COUNT);
    for (int i = 0; i < count; ++i)
    {
        Light light   = lightData.lights[i];
        int lightType = int(light.parameters.w);
        vec3 radiance = light.colorStrength.rgb * light.colorStrength.a;
        vec3 L        = vec3(0.0);

        if (lightType == DIRECTIONAL_LIGHT_TYPE)
        {
            L = normalize(-light.direction.xyz);
        }
        else if (lightType == POINT_LIGHT_TYPE)
        {
            vec3  toLight = light.position.xyz - hitPosView;
            float dist    = length(toLight);
            if (dist <= 0.0) continue;
            L = toLight / dist;
            float r   = max(light.parameters.z, 0.0001);
            float att = clamp(1.0 - dist / r, 0.0, 1.0);
            radiance *= att * att;
        }
        else if (lightType == SPOT_LIGHT_TYPE)
        {
            vec3  toLight = light.position.xyz - hitPosView;
            float dist    = length(toLight);
            if (dist <= 0.0) continue;
            L = toLight / dist;
            float r     = max(light.parameters.z, 0.0001);
            float att   = clamp(1.0 - dist / r, 0.0, 1.0);
            float theta = dot(L, normalize(-light.direction.xyz));
            float eps   = max(light.parameters.x - light.parameters.y, 0.0001);
            float spot  = clamp((theta - light.parameters.y) / eps, 0.0, 1.0);
            radiance   *= att * att * spot;
        }
        else continue;

        float NdotL = max(dot(N_view, L), 0.0);
        Lo += (albedo / PI) * NdotL * radiance;
    }

    // Firefly suppression: clamp before accumulation to prevent single-sample spikes.
    Lo = min(Lo, vec3(10.0));

    // Sky ambient at secondary hit: surfaces facing upward catch a small amount of sky light.
    // This prevents fully-enclosed areas from going completely black when no direct light reaches them.
    float skyFactor = pc.sunHeight * 0.5 + 0.5;                          // remap [-1,1] → [0,1]
    vec3  skyColor  = mix(vec3(0.01, 0.012, 0.018),                       // night
                          mix(vec3(0.55, 0.65, 0.80),                     // day sky
                              vec3(0.80, 0.50, 0.20), 0.0),               // placeholder (no golden blend needed here)
                          clamp(skyFactor, 0.0, 1.0));
    float skyVis    = max(N_world.y * 0.5 + 0.5, 0.0);                   // upper hemisphere weighting
    Lo += albedo * skyColor * skyVis * 0.06;                              // 6% sky ambient contribution

    payload.radiance = Lo;
}
