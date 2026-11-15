#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aIntensity;

uniform mat4  uMVP;
uniform float uPointSize;

out float vIntensity;
out float vHeight;

void main()
{
    gl_Position  = uMVP * vec4(aPosition, 1.0);
    gl_PointSize = uPointSize;
    vIntensity   = aIntensity;
    vHeight      = aPosition.z;
}
