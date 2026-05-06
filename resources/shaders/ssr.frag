#version 450

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uNormal;
layout(set = 0, binding = 1) uniform sampler2D uDepth;
layout(set = 0, binding = 2) uniform sampler2D uMaterial;
layout(set = 0, binding = 3) uniform sampler2D uLitColor;
layout(set = 0, binding = 4) uniform samplerCube uEnvironmentMap;
layout(set = 0, binding = 5) uniform sampler2D   uAO;

layout(push_constant) uniform PC
{
    mat4 projection;
    mat4 invProjection;
    mat4 invView;
    vec4 params0;
    vec4 params1;
    vec4 environmentInfo;
} pc;

const float kEpsilon = 1e-5;


vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 vp  = pc.invProjection * ndc;
    return vp.xyz / max(vp.w, kEpsilon);
}


vec2 viewToUV(vec3 vp)
{
    vec4 proj = pc.projection * vec4(vp, 1.0);
    proj.xyz /= max(proj.w, kEpsilon);
    return proj.xy * 0.5 + 0.5;
}


float fresnelSchlick(float cosTheta, float F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

vec3 sampleEnvironment(vec3 reflectionDirView, float roughness)
{
    if (pc.environmentInfo.x < 0.5)
        return vec3(0.0);

    vec3 reflectionDirWorld = normalize((pc.invView * vec4(reflectionDirView, 0.0)).xyz);
    float mipLevel = roughness * float(max(textureQueryLevels(uEnvironmentMap) - 1, 0));
    return textureLod(uEnvironmentMap, reflectionDirWorld, mipLevel).rgb;
}

void main()
{
    vec3 litColor = texture(uLitColor, vUV).rgb;

    if (pc.params1.w < 0.5)
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

    vec3 orm      = texture(uMaterial, vUV).rgb;
    float roughness = orm.g;
    float metallic  = orm.b;

    float roughnessCutoff = max(pc.params1.z, 0.001);


    if (roughness > roughnessCutoff)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }


    vec3 N = normalize(texture(uNormal, vUV).rgb * 2.0 - 1.0);


    vec3 viewPos = reconstructViewPos(vUV, depth);
    vec3 V       = normalize(-viewPos);
    vec3 I       = normalize(viewPos);
    vec3 R       = normalize(reflect(I, N));
    vec3 envColor = sampleEnvironment(R, roughness);


    if (length(R) < 0.5 || R.z > 0.0 || dot(R, N) <= 0.0)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }


    float maxDist  = max(pc.params0.z, 0.01);
    float thickness = clamp(pc.params0.w, 0.003, 0.16);
    int   steps    = int(pc.params1.x + 0.5);
    steps = clamp(steps, 8, 256);
    int marchSteps = min(steps * 2, 512);

    float stepLen = maxDist / float(marchSteps);
    vec3  rayPos  = viewPos + N * max(thickness * 1.5, 0.01) + R * stepLen;
    vec3  rayStep = R * stepLen;

    bool  hit    = false;
    vec2  hitUV  = vec2(0.0);
    float hitFacing = 0.0;
    float hitTravel = maxDist;

    for (int i = 0; i < marchSteps; i++)
    {
        vec2 rayUV = viewToUV(rayPos);


        if (rayUV.x < 0.0 || rayUV.x > 1.0 || rayUV.y < 0.0 || rayUV.y > 1.0)
            break;

        float sceneDepth = texture(uDepth, rayUV).r;
        if (sceneDepth >= 0.9999)
        {
            rayPos += rayStep;
            continue;
        }

        vec3  scenePos   = reconstructViewPos(rayUV, sceneDepth);
        vec3  sceneN     = normalize(texture(uNormal, rayUV).rgb * 2.0 - 1.0);


        float travel = length(rayPos - viewPos);
        float hitThickness = mix(thickness, thickness * 2.5, clamp(travel / maxDist, 0.0, 1.0));
        float depthDiff = rayPos.z - scenePos.z;
        float facing = dot(sceneN, -R);


        if (facing <= -0.5)
        {
            rayPos += rayStep;
            continue;
        }


        float normalAgreement = dot(sceneN, N);
        float planeDelta = abs(dot(scenePos - viewPos, N));
        if (normalAgreement > 0.99 && planeDelta < hitThickness * 4.0)
        {
            rayPos += rayStep;
            continue;
        }

        if (depthDiff < 0.0 && -depthDiff < hitThickness)
        {

            vec3  lo = rayPos - rayStep;
            vec3  hi = rayPos;
            for (int r = 0; r < 8; r++)
            {
                vec3  mid    = (lo + hi) * 0.5;
                vec2  midUV  = viewToUV(mid);
                if (midUV.x < 0.0 || midUV.x > 1.0 || midUV.y < 0.0 || midUV.y > 1.0)
                    break;
                float midD   = texture(uDepth, midUV).r;
                if (midD >= 0.9999)
                {
                    lo = mid;
                    continue;
                }
                vec3  midPos = reconstructViewPos(midUV, midD);
                if ((mid.z - midPos.z) < 0.0)
                    hi = mid;
                else
                    lo = mid;
            }
            hitUV = viewToUV((lo + hi) * 0.5);
            hitFacing = facing;
            hitTravel = travel;
            hit   = true;
            break;
        }

        rayPos += rayStep;
    }


    float F0      = mix(0.04, 1.0, metallic);
    float cosTheta = max(dot(-V, N), 0.0);
    float F        = fresnelSchlick(cosTheta, F0);


    float roughnessAtten = 1.0 - (roughness / roughnessCutoff);
    roughnessAtten       = roughnessAtten * roughnessAtten;

    float NdotV   = max(dot(N, V), 0.0);
    float aoDyn   = clamp(texture(uAO, vUV).r, 0.0, 1.0);
    float aoMat   = clamp(orm.r * aoDyn, 0.0, 1.0);
    float specAO  = clamp(pow(NdotV + aoMat, exp2(-16.0 * roughness - 1.0)) - 1.0 + aoMat, 0.0, 1.0);

    float reflectionWeight = clamp(F * roughnessAtten * pc.params1.y * specAO, 0.0, 1.0);

    vec3 reflectionColor = envColor;
    float hitConfidence = 0.0;
    if (hit)
    {
        vec3 hitReflectionColor = texture(uLitColor, hitUV).rgb;


        vec2 edgeFade = smoothstep(vec2(0.0), vec2(0.12), hitUV) *
                        (vec2(1.0) - smoothstep(vec2(0.88), vec2(1.0), hitUV));
        float edgeWeight = edgeFade.x * edgeFade.y;
        float facingWeight = smoothstep(0.05, 0.25, hitFacing);
        float distanceWeight = 1.0 - smoothstep(maxDist * 0.65, maxDist, hitTravel);
        hitConfidence = clamp(edgeWeight * facingWeight * distanceWeight, 0.0, 1.0);
        reflectionColor = mix(envColor, hitReflectionColor, hitConfidence);
    }
    else if (pc.environmentInfo.x < 0.5)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    outColor = vec4(mix(litColor, reflectionColor, reflectionWeight), 1.0);
}
