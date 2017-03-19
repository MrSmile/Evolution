#version 330

in vec2 coord;
in vec4 color1;
in vec4 color2;

layout(location = 0) out vec4 result;

void main()
{
    float r = length(coord.xy);  if(r > 1.0)discard;
    result = mix(color1, color2, r);
}

