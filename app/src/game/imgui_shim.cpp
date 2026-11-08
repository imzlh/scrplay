#include "imgui_shim.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "cimgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

extern "C" {

void *
sc_imgui_get_draw_data(void) {
    return static_cast<void *>(igGetDrawData());
}

bool
sc_imgui_sdl_init_for_sdl_renderer(SDL_Window *window, SDL_Renderer *renderer) {
    return ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
}

bool
sc_imgui_sdl_process_event(const SDL_Event *event) {
    return ImGui_ImplSDL2_ProcessEvent(event);
}

void
sc_imgui_sdl_new_frame(void) {
    ImGui_ImplSDL2_NewFrame();
}

void
sc_imgui_sdl_shutdown(void) {
    ImGui_ImplSDL2_Shutdown();
}

bool
sc_imgui_sdlrenderer2_init(SDL_Renderer *renderer) {
    return ImGui_ImplSDLRenderer2_Init(renderer);
}

void
sc_imgui_sdlrenderer2_new_frame(void) {
    ImGui_ImplSDLRenderer2_NewFrame();
}

void
sc_imgui_sdlrenderer2_render(void *draw_data, SDL_Renderer *renderer) {
    ImGui_ImplSDLRenderer2_RenderDrawData(
        static_cast<ImDrawData *>(draw_data), renderer);
}

void
sc_imgui_sdlrenderer2_shutdown(void) {
    ImGui_ImplSDLRenderer2_Shutdown();
}

}
