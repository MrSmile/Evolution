// graph.h : world rendering
//

#pragma once

#include "world.h"
#include <epoxy/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>



struct Camera
{
    static constexpr double scale_step = 1.0 / 16;
    static constexpr int min_scale = -32 * 4;
    static constexpr int max_scale = +32 * 4;


    int width, height;
    uint64_t x, y;
    int log_scale;
    double scale;


    void update_scale();

    Camera(SDL_Window *window);
    void apply();

    void resize(int w, int h);
    void move(int dx, int dy);
    void rescale(int delta);
};


constexpr double draw_scale = 1.0 / tile_size;

class Representation
{
    GLuint vao, vbo[3];
    size_t count[2];

    GLuint prog;
    GLint i_transform;

public:
    Representation();
    ~Representation();
    void update(const World &world);
    void draw(const World &world, const Camera &cam);
};
