#version 330

in vec4 coord;  // [x, y, rad1, rad2]
in vec3 color0;
in vec3 color1;
in vec3 color2;

layout(location = 0) out vec4 result;

void main()
{
    float r = length(coord.xy);

    vec3 color = color0;
    if(r > coord.z)color = color1;
    if(r > coord.w)color = color2;

    result = vec4(color, 1.0);
}
