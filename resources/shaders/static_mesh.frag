#version 450

const int DIRECTIONAL_LIGHT_TYPE = 0;
const int SPOT_LIGHT_TYPE = 1;
const int POINT_LIGHT_TYPE = 2;

struct Light
{
    vec4 position;        // w unused
    vec4 direction;       // w unused  
    vec4 colorStrength;   // rgb = color, a = strength
    vec4 parameters;      // x=innerCutoff, y=outerCutoff, z=radius, w=lightType
};

layout(location = 0) in vec2 fragTextureCoordinates;
layout(location = 1) in vec3 fragNormalView;
layout(location = 2) in vec3 fragPositionView;
layout(location = 3) in vec4 fragPositionLightSpace;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 2) uniform sampler2D shadowMap;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(set = 1, binding = 1) uniform MaterialColor
{
    vec4 color;
} materialColor;


layout(set = 2, binding = 0) readonly buffer LightSSBO
{
    int lightCount;
    Light lights[];
} lightData;

float calculateDirectionalLightShadow(vec3 lightDirection, vec3 normal, sampler2D shadowMapToUse)
{
    vec3 projCoords = fragPositionLightSpace.xyz / fragPositionLightSpace.w;
    vec2 texCoords = projCoords.xy * 0.5 + 0.5;
    float currentDepth = projCoords.z;

    if (texCoords.x < 0.0 || texCoords.x > 1.0 ||  texCoords.y < 0.0 || texCoords.y > 1.0 || currentDepth > 1.0)
        return 0.0;

    float cosNL = max(dot(normal, lightDirection), 0.0);
    float bias = max(0.0006 * (1.0 - cosNL), 0.00005);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMapToUse, 0));

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float pcfDepth = texture(shadowMapToUse, texCoords + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias) > pcfDepth ? 1.0 : 0.0;
        }
    }

    shadow /= 9.0;

    return shadow;
}

vec3 calculateDirectionalLight(vec3 albedo, Light light)
{
    float strength = light.colorStrength.w;

    vec3 normal = normalize(fragNormalView);
    vec3 lightDirection =  normalize(-light.direction.xyz);

    float diff = max(dot(normal, lightDirection), 0.0);
    vec3 diffuse = light.colorStrength.xyz * strength * diff;

    float shadow = calculateDirectionalLightShadow(lightDirection, normal, shadowMap);

    vec3 result = albedo * diffuse * (1.0 - shadow);

    return result;
}

vec3 calculateSpotLight(vec3 albedo, Light light)
{
    vec3 normal = normalize(fragNormalView);
    vec3 lightDirection = normalize(light.position.xyz - fragPositionView);

    float diff = max(dot(normal, lightDirection), 0.0);

    float theta = dot(lightDirection, normalize(-light.direction.xyz));
    float epsilon = light.parameters.x - light.parameters.y;
    float intensity = clamp((theta - light.parameters.y) / epsilon, 0.0, 1.0);

    float strength = light.colorStrength.w;
    vec3 color = light.colorStrength.xyz;

    vec3 diffuse = color.rgb * strength * diff;

    vec3 result = albedo * diffuse * intensity;

    return result;
}

vec3 calculatePointLight(vec3 albedo, Light light)
{
    vec3 normal = normalize(fragNormalView);
    vec3 lightDirection = normalize(light.position.xyz - fragPositionView);

    float diff = max(dot(normal, lightDirection), 0.0);

    float distance = length(light.position.xyz - fragPositionView);

    float attenuation = clamp(1.0 - (distance / light.parameters.z), 0.0, 1.0);

    vec3 diffuse = light.colorStrength.xyz * light.colorStrength.w * diff;

    vec3 result = albedo * diffuse * attenuation;

    return result;
}

void main() 
{
    vec4 textureSample = texture(texSampler, fragTextureCoordinates);
    vec3 albedo = (textureSample.a > 0.0) ? textureSample.rgb : materialColor.color.rgb;

    if (lightData.lightCount == 0)
    {
        outColor = vec4(albedo, 1.0);
        return;
    }

    vec3 ambient = albedo * 0.05;
    vec3 totalLighting = vec3(0.0);
    
    for(int i = 0; i < 16; ++i)
    {
        if (i >= lightData.lightCount)
            break;

        Light light = lightData.lights[i];

        int lightType = int(light.parameters.w);

        vec3 lightContribution;

        if(lightType == DIRECTIONAL_LIGHT_TYPE)
            lightContribution = calculateDirectionalLight(albedo, light);
        else if(lightType == SPOT_LIGHT_TYPE)
            lightContribution = calculateSpotLight(albedo, light);
        else if(lightType == POINT_LIGHT_TYPE)
            lightContribution = calculatePointLight(albedo, light);
        else
            lightContribution = vec3(0.0);

        totalLighting += lightContribution;
    }

    vec3 result = ambient + totalLighting;

    result = clamp(result, 0.0, 1.0);

    outColor = vec4(result, 1.0);
}