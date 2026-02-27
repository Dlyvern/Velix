#version 450

// Screen-Space Reflections
// Inserted between the Lighting pass and the SkyLight pass so IBL/sky is
// added on top of SSR in the next pass.
//
// set 0  = camera UBO + LightSpaceUBO + LightSSBO  (existing cameraDescriptorSetLayout)
// set 1  = HDR lighting color, G-buffer normals, G-buffer material, depth

const int DIRECTIONAL_LIGHT_TYPE = 0;
const int SPOT_LIGHT_TYPE        = 1;
const int POINT_LIGHT_TYPE       = 2;
const int MAX_LIGHT_COUNT        = 16;
const int MAX_DIRECTIONAL_CASCADES = 4;
const int MAX_SPOT_SHADOWS       = 3;

struct Light
{
    vec4 position;
    vec4 direction;
    vec4 colorStrength;
    vec4 parameters;
    vec4 shadowInfo;
};

layout(set = 0, binding = 0) uniform CameraUBO
{
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
} camera;

// Bindings 1 and 2 are declared in cameraDescriptorSetLayout but unused here.
layout(set = 0, binding = 1) uniform LightSpaceUBO
{
    mat4 lightSpaceMatrix;
    mat4 directionalLightSpaceMatrices[MAX_DIRECTIONAL_CASCADES];
    vec4 directionalCascadeSplits;
    mat4 spotLightSpaceMatrices[MAX_SPOT_SHADOWS];
} lightSpaceData;

layout(std430, set = 0, binding = 2) readonly buffer LightSSBO
{
    int   lightCount;
    Light lights[];
} lightData;

layout(set = 1, binding = 0) uniform sampler2D uHDRColor;
layout(set = 1, binding = 1) uniform sampler2D uGBufferNormal;
layout(set = 1, binding = 2) uniform sampler2D uGBufferMaterial;
layout(set = 1, binding = 3) uniform sampler2D uDepth;

layout(push_constant) uniform SSRPC
{
    float maxDistance;
    float thickness;
    float strength;
    int   steps;
    float enabled;
    float pad[3];
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// Reconstruct view-space position from depth buffer.
vec3 reconstructViewPos(vec2 uv, float depth)
{
    vec4 ndc     = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = camera.invProjection * ndc;
    return viewPos.xyz / viewPos.w;
}

// Project a view-space position back to screen-space UV.
vec2 viewToScreenUV(vec3 viewPos)
{
    vec4 clip = camera.projection * vec4(viewPos, 1.0);
    vec3 ndc  = clip.xyz / clip.w;
    return ndc.xy * 0.5 + 0.5;
}

float sampleDepth(vec2 uv)
{
    return texture(uDepth, uv).r;
}

void main()
{
    vec4 inputColor = texture(uHDRColor, vUV);
    outColor = inputColor;

    if (pc.enabled < 0.5)
        return;

    float depth = sampleDepth(vUV);
    if (depth >= 0.9999)
        return; // sky pixel – no reflection

    vec4 gN = texture(uGBufferNormal, vUV);
    vec4 gM = texture(uGBufferMaterial, vUV);

    float roughness    = gM.g;
    float metallic     = gM.b;
    float reflectivity = metallic * (1.0 - roughness * roughness);

    if (reflectivity < 0.04)
        return; // surface not reflective enough

    vec3 N_view = normalize(gN.rgb * 2.0 - 1.0);
    vec3 P_view = reconstructViewPos(vUV, depth);
    vec3 V      = normalize(-P_view);
    vec3 R      = reflect(-V, N_view);

    // Avoid marching towards the camera
    if (R.z > -0.01)
        return;

    float stepSize = pc.maxDistance / float(pc.steps);
    vec3  rayPos   = P_view + N_view * 0.05; // small bias to avoid self-hit

    vec2 hitUV  = vec2(0.0);
    bool didHit = false;

    for (int i = 1; i <= pc.steps; ++i)
    {
        rayPos += R * stepSize;

        vec2 sampleUV = viewToScreenUV(rayPos);

        // Stop if ray leaves the screen
        if (any(lessThan(sampleUV, vec2(0.0))) || any(greaterThan(sampleUV, vec2(1.0))))
            break;

        float sampleRawDepth = sampleDepth(sampleUV);
        vec3  sampleViewPos  = reconstructViewPos(sampleUV, sampleRawDepth);

        float depthDiff = rayPos.z - sampleViewPos.z;

        if (depthDiff > 0.0 && depthDiff < pc.thickness)
        {
            // Binary refinement for a sharper hit
            float lo = 0.0, hi = 1.0;
            for (int j = 0; j < 4; ++j)
            {
                float mid    = (lo + hi) * 0.5;
                vec3  midPos = (rayPos - R * stepSize) + R * stepSize * mid;
                vec2  midUV  = viewToScreenUV(midPos);
                float midD   = sampleDepth(midUV);
                vec3  midP   = reconstructViewPos(midUV, midD);
                if (midPos.z - midP.z > 0.0)
                    hi = mid;
                else
                    lo = mid;
            }
            hitUV  = viewToScreenUV((rayPos - R * stepSize) + R * stepSize * ((lo + hi) * 0.5));
            didHit = true;
            break;
        }
    }

    if (didHit)
    {
        // Screen-edge fade
        vec2 edgeFade  = 1.0 - pow(abs(hitUV * 2.0 - 1.0), vec2(6.0));
        float fade     = clamp(edgeFade.x * edgeFade.y, 0.0, 1.0);

        // Distance fade: weaker hits at the far end
        float hitDist  = length(reconstructViewPos(hitUV, sampleDepth(hitUV)) - P_view);
        float distFade = 1.0 - clamp(hitDist / pc.maxDistance, 0.0, 1.0);
        distFade       = distFade * distFade;

        float totalFade    = fade * distFade * reflectivity * pc.strength;
        vec3  refColor     = texture(uHDRColor, hitUV).rgb;
        outColor.rgb       = mix(inputColor.rgb, refColor, totalFade);
    }
}
