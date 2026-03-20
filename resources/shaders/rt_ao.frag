#version 460
#extension GL_EXT_ray_query : require

const float PI = 3.14159265359;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out float outAO;

// set 0 = cameraDescriptorSetLayout (binding 0=CameraUBO, 1=LightSpaceUBO, 2=LightSSBO, 3=TLAS)
layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

layout(set = 0, binding = 3) uniform accelerationStructureEXT uTLAS;

layout(set = 1, binding = 0) uniform sampler2D uGBufferNormal;
layout(set = 1, binding = 1) uniform sampler2D uDepth;
layout(set = 1, binding = 2) uniform sampler2D uSSAO;

layout(push_constant) uniform RTAOPC
{
    float aoRadius;
    float aoSamples;
    float enabled;
    float padding;
} pc;

float hash(vec3 p)
{
    p  = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 cosineWeightedHemisphere(float u1, float u2)
{
    float r     = sqrt(max(u1, 0.0));
    float theta = 2.0 * PI * u2;
    float x     = r * cos(theta);
    float y     = r * sin(theta);
    float z     = sqrt(max(0.0, 1.0 - u1));
    return vec3(x, y, z);
}

void buildBasis(vec3 n, out vec3 t, out vec3 b)
{
    if (abs(n.z) < 0.9999)
        t = normalize(cross(vec3(0.0, 0.0, 1.0), n));
    else
        t = normalize(cross(vec3(1.0, 0.0, 0.0), n));
    b = cross(n, t);
}

void main()
{
    float depth = texture(uDepth, vUV).r;

    if (depth >= 1.0)
    {
        outAO = 1.0;
        return;
    }
    if (pc.enabled < 0.5)
    {
        outAO = texture(uSSAO, vUV).r;
        return;
    }

    vec4 ndc     = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndc;
    viewPos     /= viewPos.w;
    vec3 P_world = (camera.invView * vec4(viewPos.xyz, 1.0)).xyz;

    vec3 N_view  = normalize(texture(uGBufferNormal, vUV).rgb * 2.0 - 1.0);
    vec3 N_world = normalize((camera.invView * vec4(N_view, 0.0)).xyz);

    vec3 origin = P_world + N_world * 0.02;

    vec3 t, b;
    buildBasis(N_world, t, b);

    int   nSamples  = max(int(pc.aoSamples), 1);
    float occlusion = 0.0;

    float px = gl_FragCoord.x;
    float py = gl_FragCoord.y;

    for (int i = 0; i < nSamples; ++i)
    {
        float u1 = hash(vec3(px, py, float(i) * 1.1731));
        float u2 = hash(vec3(py * 0.7, px * 1.31, float(i) * 2.6779));

        vec3 localDir = cosineWeightedHemisphere(u1, u2);
        vec3 worldDir = normalize(t * localDir.x + b * localDir.y + N_world * localDir.z);

        rayQueryEXT rq;
        rayQueryInitializeEXT(
            rq, uTLAS,
            gl_RayFlagsTerminateOnFirstHitEXT |
            gl_RayFlagsSkipClosestHitShaderEXT |
            gl_RayFlagsOpaqueEXT,
            0xFF,
            origin, 0.001,
            worldDir, pc.aoRadius);

        while (rayQueryProceedEXT(rq)) {}

        if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT)
        {
            float tHit = rayQueryGetIntersectionTEXT(rq, true);
            float falloff = 1.0 - clamp(tHit / pc.aoRadius, 0.0, 1.0);
            occlusion += falloff;
        }
    }

    occlusion /= float(nSamples);
    outAO = clamp(1.0 - occlusion, 0.0, 1.0);
}
