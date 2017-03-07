#version 330

in vec2 texcoord;
uniform sampler2D gui;
layout(location = 0) out vec4 result;

void main()
{
    result = texture(gui, texcoord);
}

