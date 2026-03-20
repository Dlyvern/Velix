#version 450

layout(location = 0) in  vec2 vUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D uNormal; // encoded view normal
layout(set = 0, binding = 1) uniform sampler2D uDepth;  // hardware depth [0,1]

layout(set = 0, binding = 2) uniform KernelUBO
{
    vec4 samples[64];
} kernel;

layout(push_constant) uniform PC
{
    mat4 projection;
    mat4 invProjection;
    vec4 params0; // x=texelSize.x, y=texelSize.y, z=radius, w=bias
    vec4 params1; // x=strength, y=enabled, z=samples, w=gtaoEnabled
    vec4 params2; // x=gtaoDirections, y=gtaoSteps, z=reserved, w=reserved
} pc;

vec2 texelSize()
{
    return pc.params0.xy;
}

float radius()
{
    return max(pc.params0.z, 0.001);
}

float bias()
{
    return max(pc.params0.w, 0.0);
}

float strength()
{
    return max(pc.params1.x, 0.0001);
}

int sampleCount()
{
    return clamp(int(pc.params1.z + 0.5), 4, 64);
}

bool enabled()
{
    return pc.params1.y > 0.5;
}

bool gtaoEnabled()
{
    return pc.params1.w > 0.5;
}

int gtaoDirections()
{
    return clamp(int(pc.params2.x + 0.5), 2, 8);
}

int gtaoSteps()
{
    return clamp(int(pc.params2.y + 0.5), 2, 8);
}

vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = pc.invProjection * clipPos;
    return viewPos.xyz / max(viewPos.w, 0.000001);
}

vec3 decodeViewNormal(vec2 uv)
{
    vec3 N = texture(uNormal, uv).xyz * 2.0 - 1.0;
    return normalize(N);
}

float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

mat3 buildTBN(vec3 N)
{
    vec2 jitter = vec2(hash21(vUV), hash21(vUV.yx + 19.19));
    vec3 randomVec = normalize(vec3(jitter * 2.0 - 1.0, 0.0));

    vec3 T = normalize(randomVec - N * dot(randomVec, N));
    if (length(T) < 0.001)
        T = normalize(cross(abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0), N));

    vec3 B = normalize(cross(N, T));
    return mat3(T, B, N);
}

float computeSSAO(vec3 fragPos, vec3 N)
{
    mat3 TBN = buildTBN(N);
    float occ = 0.0;
    float valid = 0.0;

    const int count = 64;
    int activeSamples = sampleCount();

    for (int i = 0; i < count; ++i)
    {
        if (i >= activeSamples)
            break;

        vec3 sampleDir = TBN * kernel.samples[i].xyz;
        vec3 samplePos = fragPos + sampleDir * radius();

        vec4 offset = pc.projection * vec4(samplePos, 1.0);
        offset.xyz /= max(offset.w, 0.000001);
        vec2 sampleUV = offset.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float sampleDepth = texture(uDepth, sampleUV).r;
        if (sampleDepth >= 0.9999)
            continue;

        vec3 sampleViewPos = reconstructViewPos(sampleUV, sampleDepth);

        vec3 delta = sampleViewPos - fragPos;
        float dist = length(delta);
        if (dist < 0.0001 || dist > radius())
            continue;

        vec3 dir = delta / dist;
        float NoDir = max(dot(N, dir), 0.0);
        float rangeWeight = smoothstep(radius(), 0.0, dist);
        float isOccluder = sampleViewPos.z > (samplePos.z + bias()) ? 1.0 : 0.0;

        float w = NoDir * rangeWeight * isOccluder;
        occ += w;
        valid += 1.0;
    }

    if (valid <= 0.0)
        return 1.0;

    return 1.0 - clamp(pow(occ / valid, strength()), 0.0, 1.0);
}

float computeGTAO(vec3 fragPos, vec3 N)
{
    float occ = 0.0;
    float valid = 0.0;

    int dirCount = gtaoDirections();
    int stepCount = gtaoSteps();

    float jitter = hash21(vUV * vec2(163.7, 97.3));
    float projScale = max(pc.projection[0][0], pc.projection[1][1]);
    float uvRadius = 0.5 * radius() * projScale / max(-fragPos.z, 0.05);
    uvRadius = clamp(uvRadius, 2.0 * max(texelSize().x, texelSize().y), 0.25);

    for (int d = 0; d < dirCount; ++d)
    {
        float angle = (6.28318530718 * (float(d) + jitter)) / float(dirCount);
        vec2 dir = vec2(cos(angle), sin(angle));

        for (int s = 1; s <= stepCount; ++s)
        {
            float t = float(s) / float(stepCount);
            vec2 sampleUV = vUV + dir * uvRadius * t;

            if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
                continue;

            float sampleDepth = texture(uDepth, sampleUV).r;
            if (sampleDepth >= 0.9999)
                continue;

            vec3 samplePos = reconstructViewPos(sampleUV, sampleDepth);
            vec3 delta = samplePos - fragPos;
            float dist = length(delta);
            if (dist < 0.0001 || dist > radius())
                continue;

            vec3 dirVS = delta / dist;
            float NoDir = max(dot(N, dirVS), 0.0);
            float closerToCamera = step(fragPos.z + bias(), samplePos.z);
            float w = NoDir * (1.0 - dist / radius()) * closerToCamera;

            occ += w;
            valid += 1.0;
        }
    }

    if (valid <= 0.0)
        return 1.0;

    return 1.0 - clamp(pow(occ / valid, strength()), 0.0, 1.0);
}

void main()
{
    float depth = texture(uDepth, vUV).r;
    if (!enabled() || depth >= 0.9999)
    {
        outAO = 1.0;
        return;
    }

    vec3 N = decodeViewNormal(vUV);
    vec3 fragPos = reconstructViewPos(vUV, depth);

    outAO = gtaoEnabled() ? computeGTAO(fragPos, N) : computeSSAO(fragPos, N);
}
