#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) out uint outObjectId;

void main()
{
    outColor = vec4(1.0, 1.0, 0.0, 1.0);
    outObjectId = 0;
}