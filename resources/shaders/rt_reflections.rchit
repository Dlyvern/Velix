#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

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

struct ReflectionInstance
{
    uint64_t     vertexAddress;
    uint64_t     indexAddress;
    uint         vertexStride;
    uint         padding0;
    uint         padding1;
    uint         padding2;
    MaterialParams material;
};

struct ReflectionPayload
{
    vec3  radiance;
    float hitT;
    uint  hit;
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

layout(std430, set = 1, binding = 6) readonly buffer ReflectionInstanceSSBO
{
    ReflectionInstance instances[];
} instanceData;

layout(set = 1, binding = 7) uniform samplerCube uEnvironmentMap;

layout(set = 2, binding = 0) uniform sampler2D allTextures[];

layout(push_constant) uniform RTPC
{
    float enableRTReflections;
    float rtReflectionSamples;
    float rtRoughnessThreshold;
    float rtReflectionStrength;
    vec3  sunDirection;
    float sunHeight;
    vec4  environmentInfo;
} pc;

layout(location = 0) rayPayloadInEXT ReflectionPayload payload;
hitAttributeEXT vec2 hitBarycentrics;

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer IndexRef  { uint  index;  };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer VertexRef
{
    vec3 position;
    vec2 uv;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
};

uint      loadIndex (uint64_t base, uint i)            { return IndexRef (base + uint64_t(i) * 4ul).index; }
VertexRef loadVertex(uint64_t base, uint stride, uint i){ return VertexRef(base + uint64_t(i) * uint64_t(stride)); }

// ---------- GGX PBR ----------
float D_GGX(float NdotH, float a2)
{
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SchlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return G_SchlickGGX(max(NdotV, 0.001), k) * G_SchlickGGX(max(NdotL, 0.001), k);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

vec3 evaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness)
{
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3  H     = normalize(V + L);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float a  = roughness * roughness;
    float a2 = a * a;

    vec3  F0 = mix(vec3(0.04), albedo, metallic);
    vec3  F  = fresnelSchlick(HdotV, F0);
    float D  = D_GGX(NdotH, a2);
    float G  = G_Smith(NdotV, NdotL, roughness);

    vec3 kD      = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse  = kD * albedo / PI;
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    return (diffuse + specular) * NdotL;
}

// ---------- Sky ----------
vec3 getSkyFallback(vec3 dir)
{
    float sunH  = pc.sunHeight;
    float h     = pow(clamp(dir.y * 0.5 + 0.5, 0.0, 1.0), 0.38);

    float dayFactor    = smoothstep(0.08, 0.28, sunH);
    float goldenFactor = 1.0 - smoothstep(-0.04, 0.22, abs(sunH - 0.07));
    float duskFactor   = 1.0 - smoothstep(-0.18, 0.02, abs(sunH + 0.06));

    vec3 zenith  = mix(mix(vec3(0.004,0.006,0.020), vec3(0.08,0.04,0.14), duskFactor),
                       mix(vec3(0.20,0.12,0.27), vec3(0.10,0.32,0.85), dayFactor), goldenFactor);
    vec3 horizon = mix(mix(vec3(0.010,0.012,0.030), vec3(0.92,0.24,0.06), duskFactor),
                       mix(vec3(1.00,0.44,0.12), vec3(0.65,0.82,1.00), dayFactor), goldenFactor);

    return mix(horizon, zenith, h);
}

vec3 sampleEnvironment(vec3 dir)
{
    return pc.environmentInfo.x > 0.5 ? texture(uEnvironmentMap, normalize(dir)).rgb
                                      : getSkyFallback(normalize(dir));
}

// ---------- Main ----------
void main()
{
    payload.hit      = 0u;
    payload.radiance = vec3(0.0);

    ReflectionInstance instance = instanceData.instances[gl_InstanceCustomIndexEXT];
    if (instance.vertexAddress == 0ul || instance.indexAddress == 0ul || instance.vertexStride == 0u)
        return;

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

    // Match UV transform from gbuffer_static.frag: scale, rotate, offset
    vec2 uv = uv0 * mat.uvTransform.xy;
    float rotRad = radians(mat.uvRotation);
    float c = cos(rotRad), s = sin(rotRad);
    uv = mat2(c, -s, s, c) * uv + mat.uvTransform.zw;

    vec3 objectNormal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);

    // Transform normal: use inverse-transpose of object-to-world
    mat3 worldToObject = mat3(gl_WorldToObjectEXT[0], gl_WorldToObjectEXT[1], gl_WorldToObjectEXT[2]);
    vec3 N_world = normalize(transpose(worldToObject) * objectNormal);

    vec3 V_world = normalize(-gl_WorldRayDirectionEXT);
    if (dot(N_world, V_world) < 0.0) N_world = -N_world;

    vec3 hitPosWorld = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 hitPosView  = (camera.view * vec4(hitPosWorld, 1.0)).xyz;
    vec3 N_view      = normalize((camera.view * vec4(N_world, 0.0)).xyz);
    vec3 V_view      = normalize((camera.view * vec4(V_world, 0.0)).xyz);

    // Sample albedo texture if available, otherwise use base color factor.
    vec3 albedo = mat.baseColorFactor.rgb;
    if (mat.albedoTexIdx > 0u)
        albedo *= texture(allTextures[nonuniformEXT(mat.albedoTexIdx)], uv).rgb;

    // Sample ORM texture (R=ao, G=roughness, B=metallic) if available.
    float roughness = mat.roughnessFactor;
    float metallic  = mat.metallicFactor;
    if (mat.ormTexIdx > 0u)
    {
        vec3 orm = texture(allTextures[nonuniformEXT(mat.ormTexIdx)], uv).rgb;
        roughness *= orm.g;
        metallic  *= orm.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic,  0.0,  1.0);

    vec3 emissive = mat.emissiveFactor.rgb;
    if (mat.emissiveTexIdx > 0u)
        emissive *= texture(allTextures[nonuniformEXT(mat.emissiveTexIdx)], uv).rgb;

    // Direct lighting
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
            float r       = max(light.parameters.z, 0.0001);
            float att     = clamp(1.0 - dist / r, 0.0, 1.0);
            float theta   = dot(L, normalize(-light.direction.xyz));
            float eps     = max(light.parameters.x - light.parameters.y, 0.0001);
            float spot    = clamp((theta - light.parameters.y) / eps, 0.0, 1.0);
            radiance     *= att * att * spot;
        }
        else continue;

        Lo += evaluateBRDF(N_view, V_view, L, albedo, metallic, roughness) * radiance;
    }

    // Environment ambient (one indirect bounce)
    vec3  F0        = mix(vec3(0.04), albedo, metallic);
    float NdotV     = max(dot(N_world, V_world), 0.0);
    vec3  F         = fresnelSchlick(NdotV, F0);
    vec3  kD        = (1.0 - F) * (1.0 - metallic);
    vec3  envDiff   = sampleEnvironment(N_world)                       * kD * albedo * 0.25;
    vec3  envSpec   = sampleEnvironment(reflect(-V_world, N_world))    * F           * 0.35;

    payload.hit      = 1u;
    payload.hitT     = gl_HitTEXT;
    payload.radiance = Lo + envDiff + envSpec + emissive;
}
