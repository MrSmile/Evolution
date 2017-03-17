#version 330

layout(location = 0) in vec2 pt;
uniform vec4 transform;  // [dx, dy, scale.x, scale.y]
uniform vec3 sel;  // [x, y, radius]
out vec4 color;

void main()
{
    vec2 pos = sel.xy + pt.xy * sel.z;
    gl_Position = vec4(transform.xy + pos * transform.zw, 0.5, 1.0);
    color = vec4(1.0, 1.0, 1.0, 1.0);
}

