#version 330

layout(location = 0) in vec2 quad;
layout(location = 1) in vec2 data;  // [offs_y, id]
layout(location = 2) in vec4 background;
uniform vec4 transform;  // [dx, dy, scale.x, scale.y]
uniform vec3 size;  // [width, height, sel_id]
out vec4 color;

void main()
{
    vec2 pos = vec2(0.0, data.x) + quad * size.xy;
    gl_Position = vec4(transform.xy + pos * transform.zw, 0.5, 1.0);
    if(data.y < -0.5 && quad.y > 0.5)gl_Position.y = -1;

    color = data.y == size.z ? vec4(0.0, 0.5, 0.5, 1.0) : background;
}
