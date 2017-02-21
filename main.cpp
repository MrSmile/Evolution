// main.cpp : entry point
//

#include "world.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <cstdio>



const char *gl_error_text()
{
    switch(glGetError())
    {
    case GL_NO_ERROR:                       return nullptr;
    case GL_INVALID_ENUM:                   return "invalid enumerant";
    case GL_INVALID_VALUE:                  return "invalid value";
    case GL_INVALID_OPERATION:              return "invalid operation";
    case GL_INVALID_FRAMEBUFFER_OPERATION:  return "invalid framebuffer operation";
    case GL_OUT_OF_MEMORY:                  return "out of memory";
    case GL_STACK_UNDERFLOW:                return "stack underflow";
    case GL_STACK_OVERFLOW:                 return "stack overflow";
    case GL_TABLE_TOO_LARGE:                return "table too large";
    default:                                return "unknown error";
    }
}

bool check_gl_error()
{
    const char *text = gl_error_text();  if(!text)return true;
    std::printf("GL error: %s\n", text);  return false;
}

bool main_loop(SDL_Window *window)
{
    //glEnable(GL_DEPTH_TEST);  glDepthFunc(GL_GREATER);  glClearDepth(0);
    glEnable(GL_FRAMEBUFFER_SRGB);  glEnable(GL_MULTISAMPLE);
    glEnable(GL_CULL_FACE);

    if(!check_gl_error())return false;

    for(SDL_Event evt;;)
    {
        if(!SDL_PollEvent(&evt))
        {
            glClearColor(0.0, 0.0, 0.0, 1.0);  glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(window);  continue;
        }
        switch(evt.type)
        {
        case SDL_MOUSEMOTION:
            if(!(evt.motion.state & SDL_BUTTON(1)))continue;
            break;

        case SDL_MOUSEWHEEL:
            break;

        case SDL_WINDOWEVENT:
            if(evt.window.event == SDL_WINDOWEVENT_RESIZED)
                glViewport(0, 0, evt.window.data1, evt.window.data2);
            break;

        case SDL_QUIT:
            return true;

        default:
            continue;
        }
    }
}

bool sdl_error(const char *text)
{
    std::printf("%s%s\n", text, SDL_GetError());  return false;
}

int init()
{
    if(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) || SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3))
        return sdl_error("Failed to set OpenGL version: ");
    if(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE))
        return sdl_error("Failed to set core profile: ");
#ifdef DEBUG
    if(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG))
        return sdl_error("Failed to use debug context: ");
#endif

    if(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1))return sdl_error("Failed to enable double-buffering: ");
    //if(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24))return sdl_error("Failed to enable depth buffer: ");
    //if(SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1))return sdl_error("Failed to enable sRGB framebuffer: ");
    if(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1) || SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4))
        return sdl_error("Failed to enable multisampling: ");

    const int width = 800, height = 600;
    SDL_Window *window = SDL_CreateWindow("Evolution",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if(!window)return sdl_error("Cannot create window: ");

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if(*SDL_GetError())return sdl_error("Cannot create OpenGL context: ");

    bool res = main_loop(window);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return res;
}

int main()
{
    if(SDL_Init(SDL_INIT_VIDEO))return sdl_error("SDL_Init failed: ");
    bool res = init();  SDL_Quit();  return res ? 0 : -1;
}
