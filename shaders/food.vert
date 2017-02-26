#version 330

layout(location = 0) in vec2 pt;
layout(location = 1) in vec4 food;  // [x, y, radius, type]
uniform vec4 transform;  // [dx, dy, scale.x, scale.y]
out vec4 color;

void main()
{
    vec2 pos = food.xy + pt.xy * food.z;
    gl_Position = vec4(transform.xy + pos * transform.zw, 0.5, 1.0);
    color = vec4(0.0, 1.0, 0.0, 1.0) + food.w * vec4(1.0, -1.0, 0.0, 0.0);
}
