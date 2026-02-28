#version 450

// -----------------------------------------------------------------------
// SMAA-lite: Subpixel Morphological Anti-Aliasing (single-pass variant)
//
// Implements the core SMAA edge-detection + neighbourhood blending in one
// pass. The technique detects luma edges, computes a blend factor along
// each edge and composites a bilinear-filtered sample against the original.
//
// When pc.enabled == 0 the pass simply copies the input unchanged.
// -----------------------------------------------------------------------

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform PC
{
    vec2  texelSize;
    float enabled;
    float edgeThreshold;   // luminance difference threshold (default 0.1)
    float maxSearchSteps;  // not used in single-pass; reserved for future
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

// -----------------------------------------------------------------------
// Luma helpers
// -----------------------------------------------------------------------

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// Fetch luma at offset (in texels)
float lumaTap(vec2 uv, vec2 offset)
{
    return luma(texture(uInput, uv + offset * pc.texelSize).rgb);
}

// -----------------------------------------------------------------------
// SMAA-lite main
// -----------------------------------------------------------------------

void main()
{
    vec4 center = texture(uInput, vUV);

    if (pc.enabled < 0.5)
    {
        outColor = center;
        return;
    }

    float lumaC = luma(center.rgb);

    // 4 cardinal neighbours
    float lumaN  = lumaTap(vUV, vec2( 0.0,  1.0));
    float lumaS  = lumaTap(vUV, vec2( 0.0, -1.0));
    float lumaE  = lumaTap(vUV, vec2( 1.0,  0.0));
    float lumaW  = lumaTap(vUV, vec2(-1.0,  0.0));

    float maxLuma  = max(lumaC, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    float minLuma  = min(lumaC, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaRange = maxLuma - minLuma;

    // Only process pixels that are on an edge
    if (lumaRange < max(pc.edgeThreshold, maxLuma * 0.125))
    {
        outColor = center;
        return;
    }

    // Diagonal neighbours for contrast check
    float lumaNW = lumaTap(vUV, vec2(-1.0,  1.0));
    float lumaNE = lumaTap(vUV, vec2( 1.0,  1.0));
    float lumaSW = lumaTap(vUV, vec2(-1.0, -1.0));
    float lumaSE = lumaTap(vUV, vec2( 1.0, -1.0));

    // Edge direction — horizontal vs vertical
    float edgeH = abs(lumaN + lumaS - 2.0 * lumaC) * 2.0
                + abs(lumaNE + lumaSE - 2.0 * lumaE)
                + abs(lumaNW + lumaSW - 2.0 * lumaW);

    float edgeV = abs(lumaE + lumaW - 2.0 * lumaC) * 2.0
                + abs(lumaNE + lumaNW - 2.0 * lumaN)
                + abs(lumaSE + lumaSW - 2.0 * lumaS);

    bool isHorizontal = edgeH >= edgeV;

    // Pick the pixel pair that straddles the edge
    float luma1 = isHorizontal ? lumaN  : lumaE;
    float luma2 = isHorizontal ? lumaS  : lumaW;
    float grad1 = abs(luma1 - lumaC);
    float grad2 = abs(luma2 - lumaC);

    // Step direction
    vec2 stepDir = isHorizontal ? vec2(0.0, pc.texelSize.y) : vec2(pc.texelSize.x, 0.0);
    if (grad1 < grad2)
        stepDir = -stepDir;

    // Blend: sample the neighbour side of the edge
    vec2 uv1 = vUV + stepDir;
    vec4 neighbour = texture(uInput, uv1);

    // Subpixel blend factor: proportional to how close to center the edge is
    float blendFactor = lumaRange / (maxLuma + 0.0001);
    blendFactor = smoothstep(0.0, 1.0, blendFactor);
    blendFactor = min(blendFactor, 0.5); // cap to avoid over-blur

    // Final blend: weighted average of center and edge sample
    outColor = mix(center, neighbour, blendFactor);
}
