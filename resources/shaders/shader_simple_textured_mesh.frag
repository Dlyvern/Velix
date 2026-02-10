#version 450

layout(location = 0) in vec2 fragTextureCoordinates;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(set = 1, binding = 1) uniform MaterialColor
{
    vec4 color;
} materialColor;

void main() 
{
    vec4 textureSample = texture(texSampler, fragTextureCoordinates);
    vec3 albedo = (textureSample.a > 0.0) ? textureSample.rgb : materialColor.color.rgb;
    outColor = vec4(albedo, 1.0);
}