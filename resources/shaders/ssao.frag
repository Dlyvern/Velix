#version 450

// -----------------------------------------------------------------------
// Screen-Space Ambient Occlusion (SSAO)
// Reads view-space normals + depth, samples a hemisphere kernel,
// outputs a single occlusion value in [0,1] (1 = fully lit, 0 = occluded).
// The Lighting pass multiplies this into the ambient term.
// -----------------------------------------------------------------------

layout(location = 0) in  vec2 vUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D uNormal;   // view-space XYZ (A unused)
layout(set = 0, binding = 1) uniform sampler2D uDepth;    // hardware depth [0,1]

layout(set = 0, binding = 2) uniform KernelUBO
{
    vec4 samples[64];
} kernel;

layout(push_constant) uniform PC
{
    mat4  projection;
    mat4  invProjection;
    vec2  texelSize;
    float radius;
    float bias;
    float strength;
    float enabled;
    int   sampleCount;
    float _pad;
} pc;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = pc.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Simple hash-based noise (no noise texture needed)
vec3 noiseVec(vec2 uv)
{
    float n = fract(sin(dot(uv * 1000.0, vec2(12.9898, 78.233))) * 43758.5453);
    float n2 = fract(sin(dot(uv * 1000.0, vec2(63.7264, 10.873))) * 38741.3517);
    return normalize(vec3(n * 2.0 - 1.0, n2 * 2.0 - 1.0, 0.0));
}

// Build a TBN matrix from normal + random tangent
mat3 buildTBN(vec3 N)
{
    vec3 noise = noiseVec(vUV);
    vec3 T = normalize(noise - N * dot(noise, N));
    vec3 B = cross(N, T);
    return mat3(T, B, N);
}

void main()
{
    if (pc.enabled < 0.5)
    {
        outAO = 1.0;
        return;
    }

    float depth = texture(uDepth, vUV).r;

    // Skip sky / background (depth == 1.0)
    if (depth >= 0.9999)
    {
        outAO = 1.0;
        return;
    }

    vec3 fragPos = reconstructViewPos(vUV, depth);

    // GBuffer normals are stored in world space — we need them in view space.
    // The normal buffer stores xyz in A-channel is roughness or similar.
    // We decode as vec3 and transform to view-space using the camera's view matrix (not passed here,
    // so we use the projection-space trick: assume normals are already view-space from GBuffer).
    vec3 normal = normalize(texture(uNormal, vUV).xyz * 2.0 - 1.0);

    mat3 TBN = buildTBN(normal);

    float occlusion = 0.0;
    int   count     = clamp(pc.sampleCount, 4, 64);

    for (int i = 0; i < count; ++i)
    {
        // Transform hemisphere sample from tangent to view space
        vec3 sampleDir = TBN * kernel.samples[i].xyz;
        vec3 samplePos = fragPos + sampleDir * pc.radius;

        // Project sample to texture space
        vec4 offset = pc.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz  = offset.xyz * 0.5 + 0.5;

        // Clamp to screen
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0)
            continue;

        float sampleDepth = texture(uDepth, offset.xy).r;
        vec3  sampleViewPos = reconstructViewPos(offset.xy, sampleDepth);

        // Range check + accumulate
        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(fragPos.z - sampleViewPos.z + 0.0001));
        occlusion += (sampleViewPos.z >= samplePos.z + pc.bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = occlusion / float(count);
    float ao  = 1.0 - pow(occlusion, pc.strength);
    outAO     = clamp(ao, 0.0, 1.0);
}
