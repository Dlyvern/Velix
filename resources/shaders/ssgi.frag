#version 450

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uNormal;
layout(set = 0, binding = 1) uniform sampler2D uDepth;
layout(set = 0, binding = 2) uniform sampler2D uMaterial;
layout(set = 0, binding = 3) uniform sampler2D uLitColor;
layout(set = 0, binding = 4) uniform sampler2D uAlbedo;

layout(push_constant) uniform PC
{
    mat4 projection;
    mat4 invProjection;
    vec4 params0;
    vec4 params1;
} pc;

const float PI = 3.14159265359;
const float kEpsilon = 1e-5;

vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 vp  = pc.invProjection * ndc;
    return vp.xyz / max(vp.w, kEpsilon);
}


float interleavedGradientNoise(vec2 pos)
{
    return fract(52.9829189 * fract(dot(pos, vec2(0.06711056, 0.00583715))));
}

void main()
{
    vec3 litColor = texture(uLitColor, vUV).rgb;

    if (pc.params1.y < 0.5)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    float depth = texture(uDepth, vUV).r;
    if (depth >= 1.0)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    vec3 N = normalize(texture(uNormal, vUV).rgb * 2.0 - 1.0);
    vec3 viewPos = reconstructViewPos(vUV, depth);
    vec3 albedo = texture(uAlbedo, vUV).rgb;
    vec3 orm = texture(uMaterial, vUV).rgb;
    float metallic = orm.b;

    float diffuseWeight = (1.0 - metallic);
    if (diffuseWeight < 0.01)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    float radius = pc.params0.z;
    float strength = pc.params1.x;
    int sampleCount = int(pc.params1.z + 0.5);
    sampleCount = clamp(sampleCount, 4, 16);

    vec2 texelSize = pc.params0.xy;



    float screenRadius = radius / max(-viewPos.z, 0.1) * pc.projection[1][1] * 0.5 / texelSize.y;
    screenRadius = clamp(screenRadius, 4.0, 128.0);


    float phi = interleavedGradientNoise(gl_FragCoord.xy) * 2.0 * PI;
    float cosPhi = cos(phi);
    float sinPhi = sin(phi);

    vec3 indirect = vec3(0.0);
    float totalWeight = 0.0;


    const float goldenAngle = 2.39996323;

    for (int i = 0; i < sampleCount; i++)
    {

        float r = sqrt((float(i) + 0.5) / float(sampleCount)) * screenRadius;
        float theta = float(i) * goldenAngle + phi;
        vec2 offset = vec2(cos(theta), sin(theta)) * r;

        vec2 sampleUV = vUV + offset * texelSize;


        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float sampleDepth = texture(uDepth, sampleUV).r;
        if (sampleDepth >= 0.9999)
            continue;

        vec3 samplePos = reconstructViewPos(sampleUV, sampleDepth);
        vec3 sampleN = normalize(texture(uNormal, sampleUV).rgb * 2.0 - 1.0);
        vec3 sampleColor = texture(uLitColor, sampleUV).rgb;


        float lum = dot(sampleColor, vec3(0.2126, 0.7152, 0.0722));
        if (lum > 2.5)
            sampleColor *= 2.5 / lum;


        vec3 diff = samplePos - viewPos;
        float dist2 = dot(diff, diff);
        if (dist2 < 0.01)
            continue;

        float dist = sqrt(dist2);
        vec3 dir = diff / dist;


        float cosReceiver = max(dot(N, dir), 0.0);

        float cosSender = max(dot(sampleN, -dir), 0.0);

        if (cosReceiver < 0.05 || cosSender < 0.05)
            continue;


        float normalSimilarity = dot(N, sampleN);
        if (normalSimilarity > 0.95)
            continue;


        float formFactor = (cosReceiver * cosSender) / (dist2 + 1.0);


        float distFade = 1.0 - smoothstep(radius * 0.5, radius, dist);

        float weight = formFactor * distFade;
        indirect += sampleColor * weight;
        totalWeight += weight;
    }


    if (totalWeight > 0.001)
        indirect /= totalWeight;
    else
        indirect = vec3(0.0);


    vec3 gi = indirect * albedo * diffuseWeight * strength;

    outColor = vec4(litColor + gi, 1.0);
}
