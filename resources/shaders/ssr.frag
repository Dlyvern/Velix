#version 450

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uNormal;   // view-space normal encoded as N*0.5+0.5
layout(set = 0, binding = 1) uniform sampler2D uDepth;    // hardware depth [0,1]
layout(set = 0, binding = 2) uniform sampler2D uMaterial; // ao(r) roughness(g) metallic(b)
layout(set = 0, binding = 3) uniform sampler2D uLitColor; // HDR scene color (after SkyLight)
layout(set = 0, binding = 4) uniform samplerCube uEnvironmentMap;

layout(push_constant) uniform PC
{
    mat4 projection;
    mat4 invProjection;
    mat4 invView;
    vec4 params0; // xy=texelSize, z=maxDistance, w=thickness
    vec4 params1; // x=steps, y=strength, z=roughnessCutoff, w=enabled
    vec4 environmentInfo; // x = hasEnvironmentMap
} pc;

const float kEpsilon = 1e-5;

// Reconstruct view-space position from UV and hardware depth.
vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 vp  = pc.invProjection * ndc;
    return vp.xyz / max(vp.w, kEpsilon);
}

// Project a view-space position to screen UV.
vec2 viewToUV(vec3 vp)
{
    vec4 proj = pc.projection * vec4(vp, 1.0);
    proj.xyz /= max(proj.w, kEpsilon);
    return proj.xy * 0.5 + 0.5;
}

// Fresnel-Schlick approximation.
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

    // Only reflective enough surfaces get SSR.
    if (roughness > roughnessCutoff)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    // Decode view-space normal (stored as N*0.5+0.5).
    vec3 N = normalize(texture(uNormal, vUV).rgb * 2.0 - 1.0);

    // Reconstruct view position and compute reflection direction.
    vec3 viewPos = reconstructViewPos(vUV, depth);
    vec3 V       = normalize(-viewPos);      // surface -> camera
    vec3 I       = normalize(viewPos);       // camera -> surface
    vec3 R       = normalize(reflect(I, N)); // surface -> reflected scene ray
    vec3 envColor = sampleEnvironment(R, roughness);

    // Reject reflections heading behind the camera or under the surface.
    if (length(R) < 0.5 || R.z > 0.0 || dot(R, N) <= 0.0)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    // Ray-march in view space.
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

        // Stop if ray left the screen.
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

        // Ray passed behind the scene surface (view Z more negative = further from camera).
        float travel = length(rayPos - viewPos);
        float hitThickness = mix(thickness, thickness * 2.5, clamp(travel / maxDist, 0.0, 1.0));
        float depthDiff = rayPos.z - scenePos.z; // negative when ray is deeper than surface
        float facing = dot(sceneN, -R);

        // Only reject completely back-facing surfaces (ray hits the inside of geometry).
        if (facing <= -0.5)
        {
            rayPos += rayStep;
            continue;
        }

        // Reject coplanar self-hits on the same reflective surface.
        float normalAgreement = dot(sceneN, N);
        float planeDelta = abs(dot(scenePos - viewPos, N));
        if (normalAgreement > 0.99 && planeDelta < hitThickness * 4.0)
        {
            rayPos += rayStep;
            continue;
        }

        if (depthDiff < 0.0 && -depthDiff < hitThickness)
        {
            // Binary refinement (8 iterations).
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

    // Fresnel weight: metals have high F0 = metallic blend of albedo (simplified: use metallic).
    float F0      = mix(0.04, 1.0, metallic);
    float cosTheta = max(dot(-V, N), 0.0);
    float F        = fresnelSchlick(cosTheta, F0);

    // Roughness falloff: smooth surfaces get full weight.
    float roughnessAtten = 1.0 - (roughness / roughnessCutoff);
    roughnessAtten       = roughnessAtten * roughnessAtten;
    float reflectionWeight = clamp(F * roughnessAtten * pc.params1.y, 0.0, 1.0);

    if (!hit)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    vec3 hitReflectionColor = texture(uLitColor, hitUV).rgb;

    // Screen-edge fade to hide the hard cutoff at the screen border.
    vec2 edgeFade = smoothstep(vec2(0.0), vec2(0.12), hitUV) *
                    (vec2(1.0) - smoothstep(vec2(0.88), vec2(1.0), hitUV));
    float edgeWeight = edgeFade.x * edgeFade.y;
    float facingWeight = smoothstep(0.05, 0.25, hitFacing);
    float distanceWeight = 1.0 - smoothstep(maxDist * 0.65, maxDist, hitTravel);
    float hitConfidence = clamp(edgeWeight * facingWeight * distanceWeight, 0.0, 1.0);
    float finalWeight = reflectionWeight * hitConfidence;

    outColor = vec4(mix(litColor, hitReflectionColor, finalWeight), 1.0);
}
