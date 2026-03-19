#version 450

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uCurrent;
layout(set = 0, binding = 1) uniform sampler2D uHistory;

layout(push_constant) uniform PC
{
    vec2  texelSize;   // 1.0 / resolution
    float blendAlpha;  // current-frame weight: 0.1 = smooth, 0.5 = responsive
    float enabled;     // 0.0 = passthrough, 1.0 = TAA active
} pc;

vec3 toYCoCg(vec3 c)
{
    return vec3(
         0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b + 0.5,
         0.5  * c.r - 0.5 * c.b + 0.5);
}

vec3 fromYCoCg(vec3 c)
{
    float Y  = c.x;
    float Co = c.y - 0.5;
    float Cg = c.z - 0.5;
    return clamp(vec3(Y + Co - Cg, Y + Cg, Y - Co - Cg), 0.0, 1.0);
}

vec3 tonemap(vec3 c)   { return c / (1.0 + dot(c, vec3(0.299, 0.587, 0.114))); }
vec3 untonemap(vec3 c) { return c / max(1.0 - dot(c, vec3(0.299, 0.587, 0.114)), 1e-4); }

vec3 clipAABB(vec3 h, vec3 boxMin, vec3 boxMax)
{
    vec3 center  = 0.5 * (boxMin + boxMax);
    vec3 halfExt = 0.5 * (boxMax - boxMin) + 1e-4;
    vec3 delta   = h - center;
    vec3 units   = delta / halfExt;
    float maxUnit = max(abs(units.x), max(abs(units.y), abs(units.z)));
    return (maxUnit > 1.0) ? center + delta / maxUnit : h;
}

void main()
{
    vec4 currentSample = texture(uCurrent, vUV);

    if (pc.enabled < 0.5)
    {
        outColor = currentSample;
        return;
    }

    vec2 ts = pc.texelSize;

    // 3x3 neighbourhood (tone-mapped for stable AABB)
    vec3 c[9];
    c[0] = tonemap(texture(uCurrent, vUV + vec2(-ts.x, -ts.y)).rgb);
    c[1] = tonemap(texture(uCurrent, vUV + vec2( 0.0,  -ts.y)).rgb);
    c[2] = tonemap(texture(uCurrent, vUV + vec2( ts.x, -ts.y)).rgb);
    c[3] = tonemap(texture(uCurrent, vUV + vec2(-ts.x,  0.0 )).rgb);
    c[4] = tonemap(currentSample.rgb);
    c[5] = tonemap(texture(uCurrent, vUV + vec2( ts.x,  0.0 )).rgb);
    c[6] = tonemap(texture(uCurrent, vUV + vec2(-ts.x,  ts.y)).rgb);
    c[7] = tonemap(texture(uCurrent, vUV + vec2( 0.0,   ts.y)).rgb);
    c[8] = tonemap(texture(uCurrent, vUV + vec2( ts.x,  ts.y)).rgb);

    // Build AABB in YCoCg
    vec3 y[9];
    for (int i = 0; i < 9; ++i)
        y[i] = toYCoCg(c[i]);

    vec3 boxMin = y[0], boxMax = y[0];
    for (int i = 1; i < 9; ++i)
    {
        boxMin = min(boxMin, y[i]);
        boxMax = max(boxMax, y[i]);
    }

    // Fetch history, clamp, un-compress
    vec3 history     = tonemap(texture(uHistory, vUV).rgb);
    vec3 historyY    = toYCoCg(history);
    historyY         = clipAABB(historyY, boxMin, boxMax);
    vec3 histClamped = untonemap(fromYCoCg(historyY));

    // Exponential moving average: current contributes blendAlpha
    vec3 result = mix(histClamped, untonemap(fromYCoCg(y[4])), pc.blendAlpha);

    outColor = vec4(result, currentSample.a);
}