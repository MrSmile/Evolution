#version 330

layout(location = 0) in vec2 quad;
layout(location = 1) in vec2 origin;
layout(location = 2) in vec4 rect;  // [tx, ty, width, height]
uniform vec4 transform;  // [dx, dy, scale.x, scale.y]
out vec2 texcoord;

void main()
{
    vec2 offs = quad * rect.zw, pos = origin + offs;
    gl_Position = vec4(transform.xy + pos * transform.zw, 0.5, 1.0);
    texcoord = (rect.xy + offs) / 128.0;
}
