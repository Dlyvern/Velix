#version 460
#extension GL_EXT_ray_query : require

const float PI = 3.14159265359;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// ---- Set 0: camera + TLAS (shared with lighting pass) ----
layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

// binding 1 & 2 exist in the layout (lightSpace, lightSSBO) but are unused here.

layout(set = 0, binding = 3) uniform accelerationStructureEXT uTLAS;

// ---- Set 1: GBuffer + previous lighting result ----
layout(set = 1, binding = 0) uniform sampler2D uGBufferNormal;
layout(set = 1, binding = 1) uniform sampler2D uGBufferAlbedo;
layout(set = 1, binding = 2) uniform sampler2D uGBufferMaterial; // ao, roughness, metallic, emissive_strength
layout(set = 1, binding = 3) uniform sampler2D uDepth;
layout(set = 1, binding = 4) uniform sampler2D uLighting;        // previous lighting output

// ---- Push constants (16 bytes) ----
layout(push_constant) uniform RTPC
{
    float enableRTReflections;   // 1.0 = on
    float rtReflectionSamples;   // rays per pixel (1=mirror, 4-8=glossy)
    float rtRoughnessThreshold;  // skip surfaces rougher than this
    float rtReflectionStrength;  // overall multiplier
} pc;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Frisvad numerically-stable orthonormal basis from any normal.
void buildOrthonormalBasis(vec3 n, out vec3 t, out vec3 b)
{
    if (n.z < -0.9999)
    {
        t = vec3(0.0, -1.0, 0.0);
        b = vec3(-1.0, 0.0, 0.0);
        return;
    }
    float a = 1.0 / (1.0 + n.z);
    float c = -n.x * n.y * a;
    t = vec3(1.0 - n.x * n.x * a, c, -n.x);
    b = vec3(c, 1.0 - n.y * n.y * a, -n.y);
}

// Per-pixel stable rotation (Jorge Jimenez 2014).
float interleavedGradientNoise()
{
    return fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
}

// Vogel disk: uniformly spreads N samples in unit disk.
vec2 vogelDiskSample(int idx, int count, float phi)
{
    const float goldenAngle = 2.4;
    float r     = sqrt(float(idx) + 0.5) / sqrt(float(count));
    float theta = float(idx) * goldenAngle + phi;
    return vec2(r * cos(theta), r * sin(theta));
}

// Schlick Fresnel.
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
void main()
{
    vec3  litColor = texture(uLighting, vUV).rgb;
    float depth    = texture(uDepth, vUV).r;

    // Fast-path: pass through if disabled or sky pixel.
    if (pc.enableRTReflections < 0.5 || depth >= 1.0)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    // Sample material parameters.
    vec4  gN        = texture(uGBufferNormal,   vUV);
    vec4  gA        = texture(uGBufferAlbedo,   vUV);
    vec4  gM        = texture(uGBufferMaterial, vUV);
    float roughness = clamp(gM.g, 0.04, 1.0);
    float metallic  = clamp(gM.b, 0.0,  1.0);

    // Only reflect surfaces with sufficient specularity.
    if (roughness > pc.rtRoughnessThreshold)
    {
        outColor = vec4(litColor, 1.0);
        return;
    }

    // Reconstruct world-space position from depth.
    vec4 ndcPos  = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndcPos;
    vec3 P_view  = viewPos.xyz / viewPos.w;
    vec3 P_world = (camera.invView * vec4(P_view, 1.0)).xyz;

    // World-space normal & view direction.
    vec3 N_view  = normalize(gN.rgb * 2.0 - 1.0);
    vec3 N_world = normalize((camera.invView * vec4(N_view, 0.0)).xyz);
    vec3 camPos  = (camera.invView * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec3 V_world = normalize(P_world - camPos);

    // Mirror reflection direction.
    vec3 R_world = reflect(V_world, N_world);

    // Bias origin slightly above the surface to avoid self-intersection.
    vec3 origin = P_world + N_world * 0.02;

    // Fresnel weighting for the reflection contribution.
    vec3 F0 = mix(vec3(0.04), gA.rgb, metallic);
    vec3 F  = fresnelSchlick(max(dot(-V_world, N_world), 0.0), F0);

    // Glossy-cone jitter basis.
    vec3 rTangent, rBitangent;
    buildOrthonormalBasis(R_world, rTangent, rBitangent);

    int   nSamples  = max(int(pc.rtReflectionSamples), 1);
    float phi       = interleavedGradientNoise() * 2.0 * PI;
    vec3  accumRefl = vec3(0.0);
    int   validHits = 0;

    for (int i = 0; i < nSamples; ++i)
    {
        // Perturb reflection direction by roughness (glossy cone).
        vec3 reflDir = R_world;
        if (roughness > 0.05 && nSamples > 1)
        {
            vec2 disk = vogelDiskSample(i, nSamples, phi) * roughness;
            reflDir   = normalize(R_world + rTangent * disk.x + rBitangent * disk.y);
            if (dot(reflDir, N_world) <= 0.0)
                reflDir = R_world; // fallback if jitter flipped below horizon
        }

        // Trace the closest hit (not TerminateOnFirstHit — we need the distance).
        rayQueryEXT rq;
        rayQueryInitializeEXT(
            rq, uTLAS,
            gl_RayFlagsOpaqueEXT,
            0xFF,
            origin, 0.01,
            reflDir, 1000.0);

        while (rayQueryProceedEXT(rq)) {}

        if (rayQueryGetIntersectionTypeEXT(rq, true) !=
            gl_RayQueryCommittedIntersectionNoneEXT)
        {
            // Project the hit point into screen space and sample the lighting buffer.
            float t      = rayQueryGetIntersectionTEXT(rq, true);
            vec3  hitPos = origin + reflDir * t;

            vec4  clip   = camera.projection * camera.view * vec4(hitPos, 1.0);
            vec3  ndc    = clip.xyz / clip.w;
            vec2  hitUV  = ndc.xy * 0.5 + 0.5;

            if (hitUV.x >= 0.0 && hitUV.x <= 1.0 &&
                hitUV.y >= 0.0 && hitUV.y <= 1.0 &&
                ndc.z   >= 0.0 && ndc.z   <= 1.0)
            {
                accumRefl += texture(uLighting, hitUV).rgb;
                ++validHits;
            }
            // Off-screen hit: contributes nothing (could fall back to skybox here).
        }
        // Ray miss: contributes nothing (sky/IBL already in the lighting buffer).
    }

    vec3 reflectionColor = (validHits > 0)
        ? (accumRefl / float(validHits))
        : vec3(0.0);

    // Add reflection on top of existing direct lighting, modulated by Fresnel.
    outColor = vec4(litColor + F * reflectionColor * pc.rtReflectionStrength, 1.0);
}
