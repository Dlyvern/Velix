#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SkyUBO
{
    vec4 sunDirection_time;    // xyz = sun direction (TO sun), w = time
    vec4 sunColor_intensity;   // rgb = sun color, w = sun intensity
    vec4 skyParams;            // x=cloudSpeed, y=cloudCoverage, z=cloudDensity, w=exposure
    vec4 lightParams;          // x=dirLightStrength, y=starIntensity, z=starDensity, w=reserved
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

// ---------------------------------------------------------------------------
// Hash / noise helpers
// ---------------------------------------------------------------------------

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
        u.y);
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;
    for (int i = 0; i < 4; ++i)
    {
        value += amplitude * noise(p * frequency);
        frequency *= 2.1;
        amplitude *= 0.5;
    }
    return value;
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

// ---------------------------------------------------------------------------
// Sky gradient — multi-layer palette
// ---------------------------------------------------------------------------

float getSunHeight(vec3 sunDir)
{
    return clamp(dot(normalize(sunDir), vec3(0.0, 1.0, 0.0)), -1.0, 1.0);
}

vec3 getSkyGradient(vec3 dir, vec3 sunDir, float sunHeight)
{
    // Day colours
    vec3 zenithDay     = vec3(0.10, 0.32, 0.85);
    vec3 horizonDay    = vec3(0.65, 0.82, 1.00);

    // Golden hour (sun 0..20 deg)
    vec3 zenithGolden  = vec3(0.14, 0.13, 0.36);
    vec3 horizonGolden = vec3(1.00, 0.55, 0.18);

    // Dusk (sun just below horizon, -12..0 deg)
    vec3 zenithDusk    = vec3(0.05, 0.04, 0.16);
    vec3 horizonDusk   = vec3(0.80, 0.25, 0.08);

    // Deep night
    vec3 zenithNight   = vec3(0.004, 0.006, 0.020);
    vec3 horizonNight  = vec3(0.010, 0.012, 0.030);

    float dayFactor    = smoothstep(0.08, 0.28, sunHeight);
    float goldenFactor = 1.0 - smoothstep(-0.04, 0.22, abs(sunHeight - 0.07));
    float duskFactor   = 1.0 - smoothstep(-0.18, 0.02, abs(sunHeight + 0.06));
    float nightFactor  = smoothstep(-0.04, -0.22, sunHeight);

    float h = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    h = pow(h, 0.38);

    // Layer from night up through golden/dusk to day
    vec3 zenith  = mix(zenithNight,  zenithDusk,   duskFactor);
    vec3 horizon = mix(horizonNight, horizonDusk,  duskFactor);

    zenith  = mix(zenith,  zenithGolden,  goldenFactor);
    horizon = mix(horizon, horizonGolden, goldenFactor);

    zenith  = mix(zenith,  zenithDay,  dayFactor);
    horizon = mix(horizon, horizonDay, dayFactor);

    vec3 sky = mix(horizon, zenith, h);

    float transitionVis = max(goldenFactor, duskFactor);
    float sunForward    = max(dot(normalize(dir), normalize(sunDir)), 0.0);

    // Warm orange glow arc near horizon on sun side
    float horizonBand = exp(-abs(dir.y) * 10.0);
    sky += vec3(1.00, 0.45, 0.12) * horizonBand * transitionVis * 0.55;

    // Broad forward luminance
    sky += vec3(1.00, 0.46, 0.14) * pow(sunForward, 4.5) * transitionVis * 0.45;
    // Tight directional core
    sky += vec3(1.00, 0.50, 0.18) * pow(sunForward, 18.0) * transitionVis * 0.35;

    // Purple/violet belt opposite the sun (the anti-crepuscular arch)
    float counterForward = max(dot(normalize(dir), -normalize(sunDir)), 0.0);
    float counterBand    = exp(-abs(dir.y) * 11.0) * pow(counterForward, 1.8);
    sky += vec3(0.35, 0.16, 0.52) * counterBand * transitionVis * 0.32;

    // Pink/lilac zenith tint at golden hour
    sky += vec3(0.32, 0.14, 0.28) * goldenFactor * clamp(dir.y, 0.0, 1.0) * 0.14;

    // Blue zenith boost during day
    sky += vec3(0.02, 0.04, 0.10) * dayFactor * clamp(dir.y, 0.0, 1.0);

    return sky;
}

// ---------------------------------------------------------------------------
// Clouds
// ---------------------------------------------------------------------------

float cloudMask(vec3 dir)
{
    float denom = max(dir.y + 0.15, 0.05);
    vec2  uv    = dir.xz / denom;
    uv += vec2(TIME_SECONDS * CLOUD_SPEED * 0.01, TIME_SECONDS * CLOUD_SPEED * 0.005);

    float c = 0.0;
    c += fbm(uv * 0.30) * 0.55;
    c += fbm(uv * 0.75) * 0.30;
    c += fbm(uv * 1.60) * 0.15;

    c = c * 0.5 + 0.5;
    float threshold = mix(0.88, 0.22, CLOUD_COVERAGE);
    float softness  = mix(0.22, 0.05, CLOUD_DENSITY);
    c = smoothstep(threshold - softness, threshold + softness, c);
    c *= smoothstep(-0.05, 0.22, dir.y);

    return clamp(c, 0.0, 1.0);
}

vec3 cloudLighting(vec3 dir, vec3 sunDir, vec3 sunColor, float sunHeight, float nightFactor)
{
    float sunDot      = max(dot(normalize(dir), normalize(sunDir)), 0.0);
    float goldenFactor = 1.0 - smoothstep(-0.04, 0.22, abs(sunHeight - 0.07));

    vec3 dayBase    = vec3(0.82, 0.86, 0.92);
    vec3 goldenBase = vec3(0.92, 0.60, 0.30);
    vec3 nightBase  = vec3(0.05, 0.07, 0.12);

    vec3 base = mix(dayBase, goldenBase, goldenFactor);
    base = mix(base, nightBase, nightFactor);

    // Directional highlight
    vec3 lit = mix(base, sunColor * 1.1, pow(sunDot, 6.0) * (1.0 - nightFactor));
    lit += sunColor * pow(sunDot, 28.0) * 0.50 * (1.0 - nightFactor);

    // Golden silver lining
    lit += vec3(1.0, 0.82, 0.50) * pow(sunDot, 55.0) * goldenFactor * 0.65;

    // Moon-lit silver on night clouds
    float moonDir = dot(normalize(dir), normalize(-sunDir + vec3(0.0, 0.3, 0.0)));
    lit += vec3(0.70, 0.78, 0.90) * max(moonDir, 0.0) * nightFactor * 0.18;

    return lit;
}

// ---------------------------------------------------------------------------
// Sun disk + corona
// ---------------------------------------------------------------------------

float sunDiskAndGlow(vec3 dir, vec3 sunDir, float lightStrength)
{
    float sunVis = clamp(lightStrength, 0.0, 1.0);
    if (sunVis <= 0.001)
        return 0.0;

    float cosA = clamp(dot(normalize(dir), normalize(sunDir)), -1.0, 1.0);
    float angle = acos(cosA);

    float diskSize = 0.0095;
    float disk     = smoothstep(diskSize * 2.0, diskSize * 0.5, angle);
    float corona1  = smoothstep(0.06,  diskSize, angle) * 0.18;
    float corona2  = smoothstep(0.20,  diskSize, angle) * 0.06;

    return disk * sunVis + (corona1 + corona2) * (0.18 + 0.22 * sunVis);
}

// ---------------------------------------------------------------------------
// Moon
// ---------------------------------------------------------------------------

float moonDisk(vec3 dir, vec3 moonDir, float nightFactor)
{
    if (nightFactor < 0.01)
        return 0.0;

    float cosA  = clamp(dot(normalize(dir), normalize(moonDir)), -1.0, 1.0);
    float angle = acos(cosA);

    float diskSize = 0.014;
    float disk     = smoothstep(diskSize * 1.8, diskSize * 0.5, angle);
    float glow     = smoothstep(0.09, diskSize, angle) * 0.05;

    return (disk * 0.92 + glow) * nightFactor;
}

// ---------------------------------------------------------------------------
// Stars
// ---------------------------------------------------------------------------

vec3 stars(vec3 dir, float nightFactor)
{
    if (nightFactor <= 0.0)
        return vec3(0.0);

    float horizonFade = smoothstep(0.02, 0.20, dir.y);
    vec3  n  = normalize(dir);
    vec2  uv = vec2(atan(n.z, n.x), asin(clamp(n.y, -1.0, 1.0)));

    float gridScale = mix(140.0, 260.0, clamp(STAR_DENSITY, 0.0, 1.0));
    vec2  p    = uv * gridScale;
    vec2  cell = floor(p);
    vec2  f    = fract(p);

    vec3 starAccum = vec3(0.0);

    const ivec2 offsets[5] = ivec2[5](
        ivec2( 0,  0),
        ivec2(-1,  0),
        ivec2( 1,  0),
        ivec2( 0, -1),
        ivec2( 0,  1));

    for (int i = 0; i < 5; ++i)
    {
        vec2  c    = cell + vec2(offsets[i]);
        float seed = hash21(c);
        if (seed < 0.965)
            continue;

        vec2  starPos  = hash22(c);
        vec2  d        = (vec2(offsets[i]) + starPos) - f;
        float dist     = length(d);
        float sizeSeed = hash21(c + 17.31);
        float radius   = mix(0.010, 0.026, sizeSeed * sizeSeed);

        float core = pow(smoothstep(radius, 0.0, dist), 8.0);
        float halo = pow(smoothstep(radius * 2.8, 0.0, dist), 2.0) * 0.07;

        float twSeed  = hash21(c + 93.7);
        float twinkle = 0.88 + 0.12 * sin(TIME_SECONDS * (0.5 + twSeed * 1.5) + twSeed * 20.0);

        float colorSeed = hash21(c + 51.2);
        vec3  starColor = mix(vec3(1.00, 0.94, 0.88), vec3(0.75, 0.86, 1.00), colorSeed);

        starAccum += starColor * (core + halo) * twinkle;
    }

    return starAccum * horizonFade * STAR_INTENSITY * nightFactor;
}

// ---------------------------------------------------------------------------
// Milky Way band
// ---------------------------------------------------------------------------

vec3 milkyWay(vec3 dir, float nightFactor)
{
    if (nightFactor < 0.05)
        return vec3(0.0);

    vec3  n    = normalize(dir);
    // Galactic plane approximation — tilted band
    float band = n.x * 0.58 + n.y * 0.30 + n.z * 0.76;
    float glow = pow(smoothstep(0.55, 0.0, abs(band)), 1.6);

    vec2 uvGal  = vec2(atan(n.z, n.x), asin(clamp(n.y, -1.0, 1.0))) * 3.5;
    float detail = fbm(uvGal * 1.4) * 0.5 + 0.5;

    float horizonFade = smoothstep(0.05, 0.25, dir.y);

    return vec3(0.12, 0.16, 0.26) * glow * detail * 0.55 * nightFactor * horizonFade;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

void main()
{
    vec3  dir    = normalize(inWorldPos);
    vec3  sunDir = normalize(SUN_DIR);

    float sunHeight = getSunHeight(sunDir);

    float lightStrength  = clamp(DIR_LIGHT_STRENGTH, 0.0, 1.0);
    float forcedNight    = 1.0 - step(0.001, DIR_LIGHT_STRENGTH);

    // Natural twilight / night — sun below horizon
    float naturalNight   = smoothstep(-0.04, -0.24, sunHeight);
    // twilight peak right at horizon crossing
    float twilightFactor = 1.0 - smoothstep(0.03, 0.25, abs(sunHeight));

    // Keep some twilight colouring even when the light is "off" so it doesn't
    // look artificial — forcedNight is reduced during the transition window.
    float nightFactor = clamp(max(naturalNight, forcedNight * (1.0 - twilightFactor * 0.40)), 0.0, 1.0);

    // ---- Sky gradient ----
    vec3 color = getSkyGradient(dir, sunDir, sunHeight);

    // ---- Night darkening (don't obliterate sunset colours) ----
    vec3 nightTint = vec3(0.006, 0.009, 0.026);
    color = mix(color, nightTint, nightFactor * 0.82);
    color *= mix(1.0, 0.10, nightFactor);

    // ---- Force-off darkness (keep slight twilight glow) ----
    color *= mix(1.0, 0.04, forcedNight * (1.0 - twilightFactor * 0.45));

    // ---- Sun disk ----
    float sunShape = sunDiskAndGlow(dir, sunDir, lightStrength);
    float sunNearHorizon = 1.0 - smoothstep(0.0, 0.25, sunHeight);
    vec3  sunCol = mix(SUN_COLOR, vec3(1.00, 0.36, 0.08), sunNearHorizon * 0.75);
    color += sunCol * sunShape * SUN_INTENSITY;

    // ---- Moon (opposite-ish the sun, offset elevation) ----
    vec3  moonDir   = normalize(-sunDir + vec3(0.0, 0.28, 0.0));
    float moonShape = moonDisk(dir, moonDir, nightFactor);
    color += vec3(0.92, 0.96, 1.00) * moonShape * 3.0;

    // ---- Milky Way ----
    color += milkyWay(dir, nightFactor);

    // ---- Clouds ----
    float c    = cloudMask(dir);
    vec3  cCol = cloudLighting(dir, sunDir, sunCol, sunHeight, nightFactor);

    float sunForward = max(dot(dir, sunDir), 0.0);
    cCol += vec3(1.0, 0.42, 0.12) * pow(sunForward, 4.0) * twilightFactor * 0.45 * lightStrength;

    color = mix(color, cCol, c * mix(0.42, 0.88, 1.0 - nightFactor * 0.72));

    // ---- Stars (dimmed behind clouds) ----
    color += stars(dir, nightFactor) * (1.0 - c * 0.85);

    // ---- Exposure ----
    color *= EXPOSURE;

    outColor = vec4(color, 1.0);
}
