#version 330

layout(location = 0) in vec2 pt;  // [r, angle]
layout(location = 1) in vec3 center;  // [x, y, radius]
layout(location = 2) in vec2 angle;   // [angle, delta]
layout(location = 3) in vec4 in_color1;
layout(location = 4) in vec4 in_color2;

uniform vec4 transform;  // [dx, dy, scale.x, scale.y]

out vec2 coord;
out vec4 color1;
out vec4 color2;

#define pi 3.14159265358979323846264338327950288

void main()
{
    float phi = (angle.x + pt.y * (angle.y + 1)) * (pi / 128.0);
    coord = vec2(cos(phi), sin(phi)) * (pt.x / cos(angle.y * (pi / 1024.0)));

    vec2 pos = center.xy + center.z * coord;
    gl_Position = vec4(transform.xy + pos * transform.zw, 0.5, 1.0);
    color1 = in_color1;  color2 = in_color2;
}
