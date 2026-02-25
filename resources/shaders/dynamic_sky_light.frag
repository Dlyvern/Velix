#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SkyUBO
{
    vec4 sunDirection_time;    // xyz = sun direction (TO sun), w = time
    vec4 sunColor_intensity;   // rgb = sun color, w = sun intensity
    vec4 skyParams;            // x=cloudSpeed, y=cloudCoverage, z=cloudDensity, w=exposure
    vec4 lightParams;            // x=dirLightStrength, y=starIntensity, z=starDensity, w=reserved
} ubo;

#define SUN_DIR        normalize(ubo.sunDirection_time.xyz)
#define TIME_SECONDS   (ubo.sunDirection_time.w)
#define SUN_COLOR      (ubo.sunColor_intensity.rgb)
#define SUN_INTENSITY  (ubo.sunColor_intensity.w)
#define CLOUD_SPEED    (ubo.skyParams.x)
#define CLOUD_COVERAGE (ubo.skyParams.y)
#define CLOUD_DENSITY  (ubo.skyParams.z)
#define EXPOSURE       (ubo.skyParams.w)

#define DIR_LIGHT_STRENGTH (ubo.lightParams.x)
#define STAR_INTENSITY      (ubo.lightParams.y)
#define STAR_DENSITY        (ubo.lightParams.z)

float hash(vec2 p)
{
    p = 50.0 * fract(p * 0.3183099 + vec2(0.71, 0.113));
    return -1.0 + 2.0 * fract(p.x * p.y * (p.x + p.y));
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);

    vec2 u = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(hash(i + vec2(0.0, 0.0)), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x),
        u.y
    );
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < 3; ++i)
    {
        value += amplitude * noise(p * frequency);
        frequency *= 2.0;
        amplitude *= 0.5;
    }

    return value;
}

float getSunHeight(vec3 sunDir)
{
    // -1 = below horizon, 0 = horizon, 1 = overhead
    return clamp(dot(normalize(sunDir), vec3(0.0, 1.0, 0.0)), -1.0, 1.0);
}

vec3 getSkyGradient(vec3 dir, vec3 sunDir, float lightStrength)
{
    float sunHeight = getSunHeight(sunDir);

    // Treat lightStrength as "sun visibility" for the sky look
    float sunVis = clamp(lightStrength, 0.0, 1.0);

    // Core palette
    vec3 zenithDay      = vec3(0.18, 0.40, 0.90);
    vec3 zenithSunset   = vec3(0.22, 0.16, 0.40);
    vec3 zenithTwilight = vec3(0.02, 0.03, 0.08);

    vec3 horizonDay      = vec3(0.82, 0.90, 1.00);
    vec3 horizonSunset   = vec3(1.00, 0.42, 0.12); // stronger orange
    vec3 horizonTwilight = vec3(0.10, 0.09, 0.14);

    // Factors
    float dayFactor      = smoothstep(0.08, 0.35, sunHeight);
    float twilightFactor = 1.0 - smoothstep(0.02, 0.20, abs(sunHeight)); // strongest near horizon
    float nightFactor    = 1.0 - smoothstep(-0.18, 0.02, sunHeight);

    // Vertical gradient
    float h = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    h = pow(h, 0.42);

    vec3 zenithColor = mix(zenithTwilight, zenithSunset, 1.0 - nightFactor);
    zenithColor      = mix(zenithColor, zenithDay, dayFactor);

    vec3 horizonColor = mix(horizonTwilight, horizonSunset, 1.0 - nightFactor);
    horizonColor      = mix(horizonColor, horizonDay, dayFactor);

    vec3 sky = mix(horizonColor, zenithColor, h);

    // --- Strong sunset band near horizon ---
    float horizonBand = exp(-abs(dir.y) * 14.0); // concentrated around horizon
    vec3 sunsetBandColor = vec3(1.00, 0.38, 0.10);
    sky += sunsetBandColor * horizonBand * twilightFactor * 0.55 * sunVis;

    // --- Warm area around sun (sunset/sunrise only) ---
    float sunForward = max(dot(normalize(dir), normalize(sunDir)), 0.0);
    float forwardGlow = pow(sunForward, 6.0);    // broad
    float forwardCore = pow(sunForward, 24.0);   // tighter

    vec3 warmGlow = vec3(1.00, 0.45, 0.18);
    sky += warmGlow * forwardGlow * twilightFactor * 0.35 * sunVis;
    sky += warmGlow * forwardCore * twilightFactor * 0.25 * sunVis;

    // Slight daylight blue boost overhead (prevents muddy daytime)
    sky += vec3(0.03, 0.05, 0.10) * dayFactor * clamp(dir.y, 0.0, 1.0);

    return sky;
}

float cloudMask(vec3 dir)
{
    float denom = max(dir.y + 0.15, 0.05);
    vec2 uv = dir.xz / denom;

    uv += vec2(TIME_SECONDS * CLOUD_SPEED * 0.01, TIME_SECONDS * CLOUD_SPEED * 0.005);

    float c = 0.0;
    c += fbm(uv * 0.35) * 0.60;
    c += fbm(uv * 0.80) * 0.30;
    c += fbm(uv * 1.80) * 0.10;

    c = c * 0.5 + 0.5; 
    float threshold = mix(0.85, 0.20, CLOUD_COVERAGE); 
    float softness  = mix(0.25, 0.05, CLOUD_DENSITY);  
    c = smoothstep(threshold - softness, threshold + softness, c);

    float horizonFade = smoothstep(-0.05, 0.20, dir.y);
    c *= horizonFade;

    return clamp(c, 0.0, 1.0);
}

vec3 cloudLighting(vec3 dir, vec3 sunDir, vec3 sunColor)
{
    float sunDot = max(dot(normalize(dir), normalize(sunDir)), 0.0);

    vec3 base = vec3(0.70, 0.74, 0.80);

    vec3 lit = mix(base, sunColor, pow(sunDot, 8.0));

    lit += sunColor * pow(sunDot, 32.0) * 0.35;

    return lit;
}

float sunDiskAndGlow(vec3 dir, vec3 sunDir, float lightStrength)
{
    float sunVis = clamp(lightStrength, 0.0, 1.0);

    if (sunVis <= 0.001)
        return 0.0;

    float cosA = clamp(dot(normalize(dir), normalize(sunDir)), -1.0, 1.0);
    float angle = acos(cosA);

    // Slightly larger/fatter near low visibility so it fades nicer
    float diskSize = mix(0.008, 0.012, 1.0 - sunVis);
    float glowSize = mix(0.07, 0.12, sunVis);

    float disk = smoothstep(diskSize * 1.8, diskSize, angle);
    float glow = smoothstep(glowSize, diskSize, angle);

    // Let the glow fall off more strongly with lower strength
    float glowStrength = 0.30 * sunVis;

    return disk * sunVis + glow * glowStrength;
}

//TODO looks like fucking rain(FIX IT)
float hash11(float p)
{
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash21(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 stars(vec3 dir, float nightFactor)
{
    if (nightFactor <= 0.0) return vec3(0.0);

    float horizonFade = smoothstep(0.02, 0.20, dir.y);

    vec3 n = normalize(dir);
    vec2 uv = vec2(atan(n.z, n.x), asin(clamp(n.y, -1.0, 1.0)));

    float gridScale = mix(140.0, 260.0, clamp(STAR_DENSITY, 0.0, 1.0));
    vec2 p = uv * gridScale;

    vec2 cell = floor(p);
    vec2 f    = fract(p);

    vec3 starAccum = vec3(0.0);

    const ivec2 sampleOffsets[5] = ivec2[5](
        ivec2(0, 0),
        ivec2(-1, 0),
        ivec2(1, 0),
        ivec2(0, -1),
        ivec2(0, 1)
    );

    // Sample a cross neighborhood instead of a full 3x3 kernel for lower cost.
    for (int i = 0; i < 5; ++i)
    {
        vec2 c = cell + vec2(sampleOffsets[i]);

        // Sparse star placement
        float seed = hash21(c);
        if (seed < 0.965) continue;

        // Random star center in this cell
        vec2 starPos = hash22(c);

        // Distance from current fragment to star center (cell-local)
        vec2 d = (vec2(sampleOffsets[i]) + starPos) - f;
        float dist = length(d);

        // Tiny point radius
        float sizeSeed = hash21(c + 17.31);
        float radius = mix(0.010, 0.028, sizeSeed * sizeSeed);

        // Sharp point core
        float core = smoothstep(radius, 0.0, dist);
        core = pow(core, 8.0);

        // Very small halo
        float halo = smoothstep(radius * 3.0, 0.0, dist);
        halo = pow(halo, 2.0) * 0.08;

        // Per-star twinkle (subtle)
        float twSeed = hash21(c + 93.7);
        float twinkle = 0.9 + 0.1 * sin(TIME_SECONDS * (0.5 + twSeed * 1.5) + twSeed * 20.0);

        // Slight warm/cool variation
        float colorSeed = hash21(c + 51.2);
        vec3 starColor = mix(
            vec3(1.00, 0.96, 0.92), // warm white
            vec3(0.78, 0.86, 1.00), // cool white
            colorSeed
        );

        starAccum += starColor * (core + halo) * twinkle;
    }

    // Fade by horizon and global night amount
    starAccum *= horizonFade * STAR_INTENSITY * nightFactor;

    return starAccum;
}

void main()
{
    vec3 dir = normalize(inWorldPos);
    vec3 sunDir = normalize(SUN_DIR);

    float sunHeight = getSunHeight(sunDir);

    // If your dir light intensity is HDR, pass a separate normalized value instead.
    // For now we treat DIR_LIGHT_STRENGTH as 0..1 "visibility".
    float lightStrength = clamp(DIR_LIGHT_STRENGTH, 0.0, 1.0);

    // Binary on/off for forced-night behavior
    float lightOn = step(0.001, DIR_LIGHT_STRENGTH);
    float forcedNight = 1.0 - lightOn;

    // Natural factors from sun position
    float twilightFactor = 1.0 - smoothstep(0.03, 0.22, abs(sunHeight)); // near horizon
    float naturalNight   = 1.0 - smoothstep(-0.15, 0.03, sunHeight);     // deep night only

    // Final night used for stars and heavy darkening
    float nightFactor = clamp(max(naturalNight, forcedNight), 0.0, 1.0);

    // Base sky (sunset-aware, uses lightStrength)
    vec3 color = getSkyGradient(dir, sunDir, lightStrength);

    // Apply darkness only in deeper night (NOT during sunset)
    vec3 nightSkyTint = vec3(0.015, 0.020, 0.040);
    color = mix(color, nightSkyTint, nightFactor * 0.70);

    // Much gentler darkening curve so sunset stays visible
    color *= mix(1.0, 0.18, nightFactor);

    // If directional light is explicitly off, force dark sky
    color *= mix(1.0, 0.06, forcedNight);

    // Sun disk + glow (now takes light strength)
    float sunShape = sunDiskAndGlow(dir, sunDir, lightStrength);
    color += SUN_COLOR * sunShape * SUN_INTENSITY;

    // Clouds
    float c = cloudMask(dir);
    vec3 cCol = cloudLighting(dir, sunDir, SUN_COLOR);

    // Blend clouds with night behavior
    vec3 nightCloud = vec3(0.035, 0.040, 0.055);

    // "Dayness" for clouds should preserve twilight a little
    float cloudDayFactor = 1.0 - clamp(max(nightFactor * 0.9, forcedNight), 0.0, 1.0);
    cCol = mix(nightCloud, cCol, cloudDayFactor);

    // Extra warm sunset tint on clouds near sun
    float sunForward = max(dot(dir, sunDir), 0.0);
    vec3 sunsetCloudTint = vec3(1.0, 0.42, 0.16);
    cCol += sunsetCloudTint * pow(sunForward, 5.0) * twilightFactor * 0.35 * lightStrength;

    // Force dim if light is disabled
    cCol *= mix(1.0, 0.22, forcedNight);

    color = mix(color, cCol, c * mix(0.40, 0.88, cloudDayFactor));

    // Stars (night only, hidden by clouds)
    color += stars(dir, nightFactor) * (1.0 - c * 0.8);

    // HDR output
    color *= EXPOSURE;

    outColor = vec4(color, 1.0);
}

// // Better point-like stars (no "rain blobs")
// vec3 stars(vec3 dir, float sunHeight)
// {
//     // Only visible at night
//     float night = 1.0 - smoothstep(-0.08, 0.08, sunHeight);
//     if (night <= 0.0) return vec3(0.0);

//     // Hide stars near horizon (atmospheric haze)
//     float horizonFade = smoothstep(0.02, 0.20, dir.y);

//     // Spherical-ish mapping (stable enough for skybox)
//     vec3 n = normalize(dir);
//     vec2 uv = vec2(atan(n.z, n.x), asin(clamp(n.y, -1.0, 1.0)));

//     // Scale controls density
//     float gridScale = mix(140.0, 260.0, clamp(STAR_DENSITY, 0.0, 1.0));
//     vec2 p = uv * gridScale;

//     vec2 cell = floor(p);
//     vec2 f    = fract(p);

//     vec3 starAccum = vec3(0.0);

//     // Check neighboring cells so stars near edges don't pop
//     for (int y = -1; y <= 1; ++y)
//     {
//         for (int x = -1; x <= 1; ++x)
//         {
//             vec2 c = cell + vec2(x, y);

//             // Whether this cell contains a star (sparse)
//             float seed = hash21(c);
//             if (seed < 0.965) // increase threshold => fewer stars
//                 continue;

//             // Random star position inside cell
//             vec2 starPos = hash22(c);

//             // Distance to star center in cell-local coords
//             vec2 d = (vec2(x, y) + starPos) - f;
//             float dist = length(d);

//             // Random star size (tiny)
//             float sizeSeed = hash21(c + 17.31);
//             float radius = mix(0.010, 0.035, sizeSeed * sizeSeed);

//             // Point core + soft halo
//             float core = smoothstep(radius, 0.0, dist);
//             core = pow(core, 6.0);

//             float halo = smoothstep(radius * 4.0, 0.0, dist);
//             halo = pow(halo, 2.0) * 0.15;

//             // Twinkle per-star (subtle)
//             float twSeed = hash21(c + 93.7);
//             float twinkle = 0.85 + 0.15 * sin(
//                 TIME_SECONDS * (0.5 + twSeed * 1.5) + twSeed * 20.0
//             );

//             // Slight color variation (mostly white/blueish)
//             float colorSeed = hash21(c + 51.2);
//             vec3 starColor = mix(
//                 vec3(1.0, 0.95, 0.90),  // warm white
//                 vec3(0.75, 0.85, 1.0),  // cool white
//                 colorSeed
//             );

//             float intensity = (core + halo) * twinkle;
//             starAccum += starColor * intensity;
//         }
//     }

//     // Final control
//     starAccum *= night * horizonFade * STAR_INTENSITY;

//     return starAccum;
// }

// void main()
// {
//     vec3 dir = normalize(inWorldPos);
//     vec3 sunDir = SUN_DIR;

//     float sunHeight = getSunHeight(sunDir);

//     // Base sky
//     vec3 color = getSkyGradient(dir, sunDir);

//     // Sun (respects directional light strength)
//     float sunShape = sunDiskAndGlow(dir, sunDir);
//     color += SUN_COLOR * sunShape * SUN_INTENSITY * DIR_LIGHT_STRENGTH;

//     // Clouds
//     float c = cloudMask(dir);
//     vec3 cCol = cloudLighting(dir, sunDir, SUN_COLOR);

//     // If sun is "off", clouds should lose warm directional tint a bit
//     vec3 neutralCloud = vec3(0.62, 0.67, 0.74);
//     cCol = mix(neutralCloud, cCol, clamp(DIR_LIGHT_STRENGTH, 0.0, 1.0));

//     color = mix(color, cCol, c * 0.85);

//     // Night stars (behind clouds)
//     color += stars(dir, sunHeight) * (1.0 - c * 0.7);

//     // Exposure (tonemap in post pass)
//     color *= EXPOSURE;

//     outColor = vec4(color, 1.0);
// }
