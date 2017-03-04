#version 330

layout(location = 0) in vec2 pt;
layout(location = 1) in vec2 center;
layout(location = 2) in vec3 radius;
layout(location = 3) in vec4 data;  // [angle, signal, energy, life]

uniform vec4 transform;  // [dx, dy, scale.x, scale.y]

out vec4 coord;  // [x, y, rad1, rad2]
out vec3 color0;
out vec3 color1;
out vec3 color2;

void main()
{
    vec2 angle = fract(data.xx + vec2(0.0, 0.25));

    vec2 sincos = angle - vec2(0.5);
    sincos = 7.59 * (0.5 - abs(sincos)) * sincos;
    sincos = (1.634 + abs(sincos)) * sincos;

    vec2 pos = pt.xy * radius.z;  coord = vec4(pos.xy, radius.xy);
    pos = center + pos * sincos.xx + pos.yx * vec2(sincos.y, -sincos.y);
    gl_Position = vec4(transform.xy + pos * transform.zw, 0.5, 1.0);

    color0 = vec3(0.0);
    uint flags = uint(255 * data.y + 0.5);
    if((flags & 16u) != 0u)color0.r = 1.0;
    if((flags & 32u) != 0u)color0.g = 1.0;
    if((flags & 64u) != 0u)color0.b = 1.0;
    color0 = mix(color0, vec3(0.5), float(flags & 1u));
    color1 = mix(vec3(0.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), data.z);
    color2 = mix(vec3(0.0, 0.0, 1.0), vec3(1.0, 1.0, 0.0), data.w);
}
