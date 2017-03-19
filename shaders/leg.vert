#version 330

layout(location = 0) in vec2 pt;  // [r, angle]
layout(location = 1) in vec3 center;  // [x, y, radius]
layout(location = 2) in float angle;
layout(location = 3) in vec4 in_color;

uniform vec4 transform;  // [dx, dy, scale.x, scale.y]

out vec4 color;

#define pi 3.14159265358979323846264338327950288

void main()
{
    float phi = (angle + pt.y) * (pi / 128.0);
    vec2 pos = center.xy + vec2(cos(phi), sin(phi)) * (pt.x * center.z);
    gl_Position = vec4(transform.xy + pos * transform.zw, 0.5, 1.0);
    color = in_color;
}
