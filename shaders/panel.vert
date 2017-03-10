#version 330

layout(location = 0) in vec2 pt;
layout(location = 1) in float stretch;
layout(location = 2) in vec2 tex;
uniform vec4 transform;  // [dx, dy, scale.x, scale.y]
uniform float height;
out vec2 texcoord;

void main()
{
    vec2 pos = pt + vec2(0.0, height * stretch);
    gl_Position = vec4(transform.xy + pos * transform.zw, 0.5, 1.0);
    texcoord = tex / 128.0;
}
