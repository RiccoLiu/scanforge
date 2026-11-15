#version 330 core
in float vIntensity;
in float vHeight;

uniform float uZMin;
uniform float uZMax;

out vec4 fragColor;

void main()
{
    // Round point sprite: cheaper and cleaner than instanced quads for 1M pts.
    vec2 d = gl_PointCoord - vec2(0.5);
    if (dot(d, d) > 0.25) discard;

    float t = clamp((vHeight - uZMin) / max(0.001, uZMax - uZMin), 0.0, 1.0);
    vec3 colour = mix(vec3(0.16, 0.48, 0.95), vec3(0.98, 0.76, 0.18), t);
    colour = mix(colour, vec3(1.0), 0.15 * clamp(vIntensity, 0.0, 1.0));
    fragColor = vec4(colour, 1.0);
}
