#ifndef SC_IMGUI_SHIM_H
#define SC_IMGUI_SHIM_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Thin C bridge over the Dear ImGui SDL2 / SDL_Renderer backends (which have
// C++ headers not parseable from scrplay's C code).

bool
sc_imgui_sdl_init_for_sdl_renderer(SDL_Window *window, SDL_Renderer *renderer);

// Current draw data (ImDrawData*), to pass to sc_imgui_sdlrenderer2_render()
void *
sc_imgui_get_draw_data(void);

bool
sc_imgui_sdl_process_event(const SDL_Event *event);

void
sc_imgui_sdl_new_frame(void);

void
sc_imgui_sdl_shutdown(void);

bool
sc_imgui_sdlrenderer2_init(SDL_Renderer *renderer);

void
sc_imgui_sdlrenderer2_new_frame(void);

// draw_data is an ImDrawData* obtained from sc_imgui_get_draw_data()
void
sc_imgui_sdlrenderer2_render(void *draw_data, SDL_Renderer *renderer);

void
sc_imgui_sdlrenderer2_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
