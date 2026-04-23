#version 450

layout(location = 0) in vec3 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube skybox;

void main()
{
    vec3 color = texture(skybox, normalize(inTexCoord)).rgb;






    float exposure = 1.0;







    color = pow(color, vec3(1.0/2.2));

    outColor = vec4(color, 1.0);
}