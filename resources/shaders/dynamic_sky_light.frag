#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SkyUBO
{
    vec4 sunDirection_time;
    vec4 sunColor_intensity;
    vec4 skyParams;
    vec4 lightParams;
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

const float PI = 3.14159265359;





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

float hash3(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

float noise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(mix(hash3(i),                 hash3(i + vec3(1,0,0)), u.x),
            mix(hash3(i + vec3(0,1,0)),   hash3(i + vec3(1,1,0)), u.x), u.y),
        mix(mix(hash3(i + vec3(0,0,1)),   hash3(i + vec3(1,0,1)), u.x),
            mix(hash3(i + vec3(0,1,1)),   hash3(i + vec3(1,1,1)), u.x), u.y),
        u.z);
}

float fbm3(vec3 p)
{
    float value = 0.0;
    float amp   = 0.5;
    float freq  = 1.0;
    for (int i = 0; i < 4; ++i)
    {
        value += amp * noise3(p * freq);
        freq  *= 2.1;
        amp   *= 0.5;
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






vec3 getSkyGradient(vec3 dir, vec3 sunDir, float sunHeight)
{
    float mu = dot(dir, sunDir);
    float y  = max(dir.y, 0.001);


    float opticalDepth = exp(-y * 3.0) + 0.03;


    vec3 betaR = vec3(5.8e-3, 13.5e-3, 33.1e-3);
    float betaM = 8.0e-3;


    float phaseR = 0.75 * (1.0 + mu * mu);

    float g = 0.76;
    float phaseM = (1.0 - g*g) / (4.0 * PI * pow(1.0 + g*g - 2.0*g*mu, 1.5));



    float sunOpticalDepth = exp(-sunHeight * 3.5) + 0.08;
    vec3 sunTransmittance = exp(-betaR * sunOpticalDepth * 8.0);


    vec3 extinction = exp(-(betaR + betaM) * opticalDepth * 4.0);


    vec3 rayleigh = betaR * phaseR;
    vec3 mie      = vec3(betaM) * phaseM;


    float scatterFade = smoothstep(-0.15, 0.05, sunHeight);
    vec3 inscatter = (rayleigh + mie) * sunTransmittance * (1.0 - extinction) * scatterFade;

    vec3 sky = inscatter * 32.0;


    sky += vec3(0.03, 0.06, 0.16) * smoothstep(0.0, 0.7, y) * clamp(sunHeight + 0.1, 0.0, 1.0);


    float horizonHaze = exp(-abs(dir.y) * 4.0);
    vec3 hazeColor = mix(vec3(0.70, 0.75, 0.85), sunTransmittance * 1.5 + vec3(0.15, 0.08, 0.03), 0.5);
    sky += hazeColor * horizonHaze * 0.12 * clamp(sunHeight + 0.15, 0.0, 1.0);


    float minBrightness = clamp(sunHeight * 0.5 + 0.12, 0.02, 0.2);
    vec3 minSky = vec3(0.04, 0.05, 0.08) * minBrightness;
    sky = max(sky, minSky);


    if (dir.y < 0.0)
    {
        float belowFade = smoothstep(-0.35, 0.0, dir.y);
        vec3 horizonColor = sky;
        vec3 groundColor = mix(vec3(0.01, 0.012, 0.025), horizonColor, belowFade * belowFade);
        sky = groundColor;
    }

    return sky;
}





vec3 sunsetEnhancements(vec3 sky, vec3 dir, vec3 sunDir, float sunHeight)
{
    float sunForward = max(dot(dir, sunDir), 0.0);
    float goldenFactor = 1.0 - smoothstep(-0.04, 0.22, abs(sunHeight - 0.07));
    float lowSun = 1.0 - smoothstep(0.06, 0.34, sunHeight);
    float transitionVis = max(goldenFactor, smoothstep(-0.12, 0.0, sunHeight) * lowSun);


    float horizonBand = exp(-abs(dir.y) * 8.0);
    sky += vec3(1.00, 0.42, 0.10) * horizonBand * transitionVis * 0.45;


    sky += vec3(1.00, 0.44, 0.12) * pow(sunForward, 3.6) * transitionVis * 0.40;

    sky += vec3(1.00, 0.46, 0.14) * pow(sunForward, 14.0) * transitionVis * 0.30;


    float warmBand = exp(-abs(dir.y) * 6.0);
    sky += vec3(1.00, 0.34, 0.08) * warmBand * pow(sunForward, 1.45) * lowSun * 0.35;


    float counterForward = max(dot(dir, -sunDir), 0.0);
    float counterBand    = exp(-abs(dir.y) * 11.0) * pow(counterForward, 1.8);
    sky += vec3(0.33, 0.14, 0.44) * counterBand * transitionVis * 0.18;


    sky += vec3(0.32, 0.14, 0.28) * goldenFactor * clamp(dir.y, 0.0, 1.0) * 0.10;

    return sky;
}





float worley3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);

    float minDist = 1.0;

    for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
    for (int z = -1; z <= 1; z++)
    {
        vec3 neighbor = vec3(float(x), float(y), float(z));

        vec3 cellPos = vec3(
            hash3(i + neighbor),
            hash3(i + neighbor + 31.17),
            hash3(i + neighbor + 67.31)
        );
        vec3 diff = neighbor + cellPos - f;
        float dist = dot(diff, diff);
        minDist = min(minDist, dist);
    }

    return sqrt(minDist);
}





vec3 cloudUV(vec3 dir)
{
    vec3 uv = normalize(dir);


    uv.x += TIME_SECONDS * CLOUD_SPEED * 0.01;
    uv.z += TIME_SECONDS * CLOUD_SPEED * 0.005;


    uv.y *= 3.0;


    float warpX = noise3(uv * 2.8 + vec3(17.1, 0.0, 0.0));
    float warpZ = noise3(uv * 2.8 + vec3(0.0, 0.0, 43.2));
    uv.x += warpX * 0.30;
    uv.z += warpZ * 0.30;

    return uv;
}

float cloudMask(vec3 dir)
{
    vec3 uv = cloudUV(dir);


    float cells = 1.0 - worley3(uv * 4.0);
    float cells2 = 1.0 - worley3(uv * 8.0);
    float shape = cells * 0.55 + cells2 * 0.45;


    float erosion1 = fbm3(uv * 6.0);
    float erosion2 = fbm3(uv * 14.0);

    float c = shape - erosion1 * 0.42 - erosion2 * 0.22;


    float threshold = mix(0.38, 0.14, CLOUD_COVERAGE);
    float softness  = mix(0.16, 0.08, CLOUD_DENSITY);
    c = smoothstep(threshold, threshold + softness, c);


    c *= smoothstep(-0.05, 0.18, dir.y);


    float internalVariation = noise3(uv * 18.0) * 0.30 + 0.70;
    c *= internalVariation;

    return clamp(c, 0.0, 1.0);
}


float cloudShadow(vec3 dir, vec3 sunDir)
{

    vec3 shadowDir = normalize(dir + sunDir * 0.1);
    vec3 uv = normalize(shadowDir);
    uv.x += TIME_SECONDS * CLOUD_SPEED * 0.01;
    uv.z += TIME_SECONDS * CLOUD_SPEED * 0.005;
    uv.y *= 3.0;

    float cells = 1.0 - worley3(uv * 4.0);
    float erosion = noise3(uv * 6.0) * 0.5 + 0.5;
    float shadow = cells - erosion * 0.3;

    return clamp(shadow, 0.0, 1.0);
}

vec3 cloudLighting(vec3 dir, vec3 sunDir, vec3 sunColor, float sunHeight, float nightFactor, float selfShadow, float density)
{
    float sunDot       = max(dot(normalize(dir), normalize(sunDir)), 0.0);
    float goldenFactor = 1.0 - smoothstep(-0.04, 0.22, abs(sunHeight - 0.07));
    float lowSun       = 1.0 - smoothstep(0.08, 0.35, sunHeight);


    vec3 dayBase    = vec3(0.82, 0.85, 0.92);
    vec3 goldenBase = vec3(0.95, 0.55, 0.25);
    vec3 nightBase  = vec3(0.04, 0.05, 0.09);

    vec3 base = mix(dayBase, goldenBase, goldenFactor);
    base = mix(base, nightBase, nightFactor);


    float shadowFactor = mix(0.4, 1.0, selfShadow);
    base *= shadowFactor;


    base *= mix(0.55, 1.0, density);


    vec3 lit = base;
    lit += sunColor * 0.25 * pow(sunDot, 4.0) * (1.0 - nightFactor);


    float edgeFactor = 1.0 - density;
    lit += vec3(1.0, 0.97, 0.90) * pow(sunDot, 8.0) * 0.4 * (1.0 - nightFactor) * edgeFactor;


    lit += vec3(1.0, 0.76, 0.40) * pow(sunDot, 32.0) * goldenFactor * 0.6;


    lit += vec3(0.95, 0.36, 0.12) * pow(sunDot, 3.0) * lowSun * (1.0 - nightFactor) * 0.4;
    float bottomTint = smoothstep(0.25, 0.05, dir.y);
    lit += vec3(0.9, 0.38, 0.14) * bottomTint * lowSun * (1.0 - nightFactor) * 0.25;


    float moonDot = dot(normalize(dir), normalize(-sunDir + vec3(0.0, 0.3, 0.0)));
    lit += vec3(0.60, 0.68, 0.82) * max(moonDot, 0.0) * nightFactor * 0.15;

    return lit;
}





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





vec3 milkyWay(vec3 dir, float nightFactor)
{
    if (nightFactor < 0.05)
        return vec3(0.0);

    vec3  n    = normalize(dir);
    float band = n.x * 0.58 + n.y * 0.30 + n.z * 0.76;
    float glow = pow(smoothstep(0.55, 0.0, abs(band)), 1.6);

    vec2 uvGal  = vec2(atan(n.z, n.x), asin(clamp(n.y, -1.0, 1.0))) * 3.5;
    float detail = fbm(uvGal * 1.4) * 0.5 + 0.5;

    float horizonFade = smoothstep(0.05, 0.25, dir.y);

    return vec3(0.12, 0.16, 0.26) * glow * detail * 0.55 * nightFactor * horizonFade;
}





void main()
{
    vec3  dir    = normalize(inWorldPos);
    vec3  sunDir = normalize(SUN_DIR);

    float sunHeight = clamp(dot(sunDir, vec3(0.0, 1.0, 0.0)), -1.0, 1.0);

    float lightStrength  = clamp(DIR_LIGHT_STRENGTH, 0.0, 1.0);
    float forcedNight    = 1.0 - step(0.001, DIR_LIGHT_STRENGTH);

    float naturalNight   = smoothstep(-0.04, -0.24, sunHeight);
    float twilightFactor = 1.0 - smoothstep(0.03, 0.25, abs(sunHeight));
    float nightFactor    = clamp(max(naturalNight, forcedNight * (1.0 - twilightFactor * 0.40)), 0.0, 1.0);


    vec3 color = getSkyGradient(dir, sunDir, sunHeight);


    color = sunsetEnhancements(color, dir, sunDir, sunHeight);


    vec3 nightTint = vec3(0.006, 0.009, 0.026);
    float nightBlend = nightFactor * mix(0.82, 0.52, twilightFactor);
    color = mix(color, nightTint, nightBlend);
    color *= mix(1.0, mix(0.10, 0.28, twilightFactor), nightFactor);


    color *= mix(1.0, 0.04, forcedNight * (1.0 - twilightFactor * 0.45));


    float sunHorizonFade = smoothstep(-0.05, 0.02, sunHeight);
    float sunShape = sunDiskAndGlow(dir, sunDir, lightStrength * sunHorizonFade);
    float sunNearHorizon = 1.0 - smoothstep(0.0, 0.25, sunHeight);
    vec3  sunCol = mix(SUN_COLOR, vec3(1.00, 0.28, 0.05), sunNearHorizon * 0.92);
    sunCol *= mix(1.0, 1.15, sunNearHorizon);
    color += sunCol * sunShape * SUN_INTENSITY * sunHorizonFade;


    vec3  moonDir   = normalize(-sunDir + vec3(0.0, 0.28, 0.0));
    float moonShape = moonDisk(dir, moonDir, nightFactor);
    color += vec3(0.92, 0.96, 1.00) * moonShape * 3.0;


    color += milkyWay(dir, nightFactor);


    float c    = cloudMask(dir);
    float shadow = cloudShadow(dir, sunDir);
    vec3  cCol = cloudLighting(dir, sunDir, sunCol, sunHeight, nightFactor, shadow, c);

    float sunForward = max(dot(dir, sunDir), 0.0);
    cCol += vec3(1.0, 0.34, 0.09) * pow(sunForward, 3.2) * twilightFactor * 0.78 * lightStrength;

    color = mix(color, cCol, c * mix(0.42, 0.88, 1.0 - nightFactor * 0.72));


    color += stars(dir, nightFactor) * (1.0 - c * 0.85);


    color *= EXPOSURE;

    outColor = vec4(color, 1.0);
}
