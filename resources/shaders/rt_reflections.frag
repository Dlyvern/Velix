#version 460
#extension GL_EXT_ray_query         : require
#extension GL_EXT_buffer_reference2  : require
#extension GL_EXT_scalar_block_layout: require
#extension GL_ARB_gpu_shader_int64   : require

const int   DIRECTIONAL_LIGHT_TYPE = 0;
const int   SPOT_LIGHT_TYPE        = 1;
const int   POINT_LIGHT_TYPE       = 2;
const int   MAX_LIGHT_COUNT        = 16;
const float PI                     = 3.14159265359;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

struct Light
{
    vec4 position;
    vec4 direction;
    vec4 colorStrength;
    vec4 parameters;
    vec4 shadowInfo;
};

layout(std430, set = 0, binding = 2) readonly buffer LightData
{
    int   lightCount;
    Light lights[];
} lightData;

layout(set = 0, binding = 3) uniform accelerationStructureEXT uTLAS;

layout(set = 1, binding = 0) uniform sampler2D uGBufferNormal;
layout(set = 1, binding = 1) uniform sampler2D uGBufferAlbedo;
layout(set = 1, binding = 2) uniform sampler2D uGBufferMaterial;
layout(set = 1, binding = 3) uniform sampler2D uDepth;
layout(set = 1, binding = 4) uniform sampler2D uLighting;
layout(set = 1, binding = 7) uniform samplerCube uEnvironmentMap;

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

layout(std430, set = 1, binding = 6) readonly buffer ReflectionInstanceSSBO
{
    ReflectionInstance instances[];
} instanceData;

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

layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer IndexRef  { uint  index;  };
layout(buffer_reference, scalar, buffer_reference_align = 4) readonly buffer VertexRef
{
    vec3 position;
    vec2 uv;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
};

uint      loadIndex (uint64_t base, uint i)             { return IndexRef (base + uint64_t(i) * 4ul).index; }
VertexRef loadVertex(uint64_t base, uint stride, uint i) { return VertexRef(base + uint64_t(i) * uint64_t(stride)); }

// ---------- GGX PBR ----------
float D_GGX(float NdotH, float a2)
{
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_SchlickGGX(float NdotX, float k) { return NdotX / (NdotX * (1.0 - k) + k); }

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
    float a2    = pow(roughness * roughness, 2.0);

    vec3  F0      = mix(vec3(0.04), albedo, metallic);
    vec3  F       = fresnelSchlick(HdotV, F0);
    float D       = D_GGX(NdotH, a2);
    float G       = G_Smith(NdotV, NdotL, roughness);
    vec3  kD      = (1.0 - F) * (1.0 - metallic);
    vec3  diffuse  = kD * albedo / PI;
    vec3  specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);

    return (diffuse + specular) * NdotL;
}

// ---------- Sky ----------
vec3 getSkyFallback(vec3 dir)
{
    float sunH = pc.sunHeight;
    float h    = pow(clamp(dir.y * 0.5 + 0.5, 0.0, 1.0), 0.38);

    float dayFactor    = smoothstep(0.08, 0.28, sunH);
    float goldenFactor = 1.0 - smoothstep(-0.04, 0.22, abs(sunH - 0.07));
    float duskFactor   = 1.0 - smoothstep(-0.18, 0.02, abs(sunH + 0.06));

    vec3 zenith  = mix(mix(vec3(0.004,0.006,0.020), vec3(0.08,0.04,0.14), duskFactor),
                       mix(vec3(0.20,0.12,0.27),     vec3(0.10,0.32,0.85), dayFactor), goldenFactor);
    vec3 horizon = mix(mix(vec3(0.010,0.012,0.030), vec3(0.92,0.24,0.06), duskFactor),
                       mix(vec3(1.00,0.44,0.12),     vec3(0.65,0.82,1.00), dayFactor), goldenFactor);

    return mix(horizon, zenith, h);
}

vec3 sampleEnvironment(vec3 dir)
{
    return pc.environmentInfo.x > 0.5 ? texture(uEnvironmentMap, normalize(dir)).rgb
                                      : getSkyFallback(normalize(dir));
}

// ---------- Shade a ray-query hit (mirrors rchit logic) ----------
vec3 shadeHit(int instanceIdx, int primitiveIdx, vec2 bary, mat4x3 worldToObj,
              vec3 hitPosWorld, vec3 rayDir)
{
    ReflectionInstance instance = instanceData.instances[instanceIdx];
    if (instance.vertexAddress == 0ul || instance.indexAddress == 0ul || instance.vertexStride == 0u)
        return vec3(0.0);

    uint baseIdx = uint(primitiveIdx) * 3u;
    uint i0 = loadIndex(instance.indexAddress, baseIdx + 0u);
    uint i1 = loadIndex(instance.indexAddress, baseIdx + 1u);
    uint i2 = loadIndex(instance.indexAddress, baseIdx + 2u);

    VertexRef v0 = loadVertex(instance.vertexAddress, instance.vertexStride, i0);
    VertexRef v1 = loadVertex(instance.vertexAddress, instance.vertexStride, i1);
    VertexRef v2 = loadVertex(instance.vertexAddress, instance.vertexStride, i2);

    vec3 b = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
    vec3 objectNormal = normalize(v0.normal * b.x + v1.normal * b.y + v2.normal * b.z);

    mat3 w2o     = mat3(worldToObj[0], worldToObj[1], worldToObj[2]);
    vec3 N_world = normalize(transpose(w2o) * objectNormal);
    vec3 V_world = normalize(-rayDir);
    if (dot(N_world, V_world) < 0.0) N_world = -N_world;

    vec3 hitPosView = (camera.view * vec4(hitPosWorld, 1.0)).xyz;
    vec3 N_view     = normalize((camera.view * vec4(N_world, 0.0)).xyz);
    vec3 V_view     = normalize((camera.view * vec4(V_world, 0.0)).xyz);

    vec3  albedo    = instance.material.baseColorFactor.rgb;
    vec3  emissive  = instance.material.emissiveFactor.rgb;
    float roughness = clamp(instance.material.roughnessFactor, 0.04, 1.0);
    float metallic  = clamp(instance.material.metallicFactor,  0.0,  1.0);

    // Direct lighting with GGX BRDF
    vec3 Lo    = vec3(0.0);
    int  count = min(lightData.lightCount, MAX_LIGHT_COUNT);
    for (int i = 0; i < count; ++i)
    {
        Light light   = lightData.lights[i];
        int   ltype   = int(light.parameters.w);
        vec3  radiance = light.colorStrength.rgb * light.colorStrength.a;
        vec3  L        = vec3(0.0);

        if (ltype == DIRECTIONAL_LIGHT_TYPE)
        {
            L = normalize(-light.direction.xyz);
        }
        else if (ltype == POINT_LIGHT_TYPE)
        {
            vec3  toLight = light.position.xyz - hitPosView;
            float dist    = length(toLight);
            if (dist <= 0.0) continue;
            L = toLight / dist;
            float r   = max(light.parameters.z, 0.0001);
            float att = clamp(1.0 - dist / r, 0.0, 1.0);
            radiance *= att * att;
        }
        else if (ltype == SPOT_LIGHT_TYPE)
        {
            vec3  toLight = light.position.xyz - hitPosView;
            float dist    = length(toLight);
            if (dist <= 0.0) continue;
            L = toLight / dist;
            float r    = max(light.parameters.z, 0.0001);
            float att  = clamp(1.0 - dist / r, 0.0, 1.0);
            float th   = dot(L, normalize(-light.direction.xyz));
            float eps  = max(light.parameters.x - light.parameters.y, 0.0001);
            float spot = clamp((th - light.parameters.y) / eps, 0.0, 1.0);
            radiance  *= att * att * spot;
        }
        else continue;

        Lo += evaluateBRDF(N_view, V_view, L, albedo, metallic, roughness) * radiance;
    }

    // Environment ambient
    vec3  F0      = mix(vec3(0.04), albedo, metallic);
    float NdotV   = max(dot(N_world, V_world), 0.0);
    vec3  F       = fresnelSchlick(NdotV, F0);
    vec3  kD      = (1.0 - F) * (1.0 - metallic);
    vec3  envDiff = sampleEnvironment(N_world)                    * kD * albedo * 0.25;
    vec3  envSpec = sampleEnvironment(reflect(-V_world, N_world)) * F           * 0.35;

    return Lo + envDiff + envSpec + emissive;
}

void buildOrthonormalBasis(vec3 n, out vec3 t, out vec3 b)
{
    if (n.z < -0.9999) { t = vec3(0,-1,0); b = vec3(-1,0,0); return; }
    float a = 1.0 / (1.0 + n.z);
    float c = -n.x * n.y * a;
    t = vec3(1.0 - n.x*n.x*a, c, -n.x);
    b = vec3(c, 1.0 - n.y*n.y*a, -n.y);
}

float hash13(vec3 p) { p = fract(p*0.1031); p += dot(p, p.yzx+33.33); return fract((p.x+p.y)*p.z); }

void main()
{
    vec3  litColor  = texture(uLighting, vUV).rgb;
    float depth     = texture(uDepth,    vUV).r;

    if (pc.enableRTReflections < 0.5 || depth >= 1.0)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    vec4  gN        = texture(uGBufferNormal,   vUV);
    vec4  gA        = texture(uGBufferAlbedo,   vUV);
    vec4  gM        = texture(uGBufferMaterial, vUV);
    float roughness  = clamp(gM.g, 0.04, 1.0);
    float metallic   = clamp(gM.b, 0.0,  1.0);

    if (roughness > pc.rtRoughnessThreshold)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    vec4 viewPos = camera.invProjection * vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec3 P_world = (camera.invView * vec4(viewPos.xyz / viewPos.w, 1.0)).xyz;
    vec3 N_view  = normalize(gN.rgb * 2.0 - 1.0);
    vec3 N_world = normalize((camera.invView * vec4(N_view, 0.0)).xyz);
    vec3 camPos  = (camera.invView * vec4(0,0,0,1)).xyz;
    vec3 V       = normalize(camPos - P_world);
    vec3 R       = reflect(-V, N_world);

    vec3 F0 = mix(vec3(0.04), gA.rgb, metallic);
    vec3 F  = fresnelSchlick(max(dot(V, N_world), 0.0), F0);

    vec3 rT, rB;
    buildOrthonormalBasis(R, rT, rB);

    int   nSamples = max(int(pc.rtReflectionSamples), 1);
    float phi      = hash13(P_world) * 2.0 * PI;
    vec3  accum    = vec3(0.0);
    float bias     = 0.005;

    for (int i = 0; i < nSamples; ++i)
    {
        vec3 reflDir = R;
        if (roughness > 0.05 && nSamples > 1)
        {
            float r_     = sqrt(float(i) + 0.5) / sqrt(float(nSamples));
            float theta  = float(i) * 2.4 + phi;
            vec2  disk   = vec2(r_ * cos(theta), r_ * sin(theta)) * roughness;
            reflDir = normalize(R + rT * disk.x + rB * disk.y);
            if (dot(reflDir, N_world) <= 0.0) reflDir = R;
        }

        vec3 origin = P_world + N_world * bias;

        rayQueryEXT rq;
        rayQueryInitializeEXT(rq, uTLAS,
                              gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
                              0xFF, origin, 0.001, reflDir, 1000.0);
        while (rayQueryProceedEXT(rq)) {}

        if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT)
        {
            accum += sampleEnvironment(reflDir);
        }
        else
        {
            float    t          = rayQueryGetIntersectionTEXT(rq, true);
            int      instIdx    = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
            int      primIdx    = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
            vec2     bary       = rayQueryGetIntersectionBarycentricsEXT(rq, true);
            mat4x3   worldToObj = rayQueryGetIntersectionWorldToObjectEXT(rq, true);
            vec3     hitPos     = origin + reflDir * t;

            accum += shadeHit(instIdx, primIdx, bary, worldToObj, hitPos, reflDir);
        }
    }

    vec3 reflectionColor = accum / float(nSamples);
    outColor = vec4(litColor + F * reflectionColor * pc.rtReflectionStrength, 1.0);
}
