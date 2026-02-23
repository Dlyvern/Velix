#version 450

layout(location = 0) in vec3 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube skybox;

void main() 
{
    vec3 color = texture(skybox, normalize(inTexCoord)).rgb;
    
    // Tone mapping (optional - choose one)
    // 1. Reinhard tone mapping
    // color = color / (color + vec3(1.0));
    
    // 2. Exposure-based tone mapping
    float exposure = 1.0;
    // color = vec3(1.0) - exp(-color * exposure);
    
    // 3. ACES approximation (good for HDR)
    // color = color * 0.6;
    // color = (color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14);
    
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(color, 1.0);
}