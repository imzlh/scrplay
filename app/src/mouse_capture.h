#ifndef SC_MOUSE_CAPTURE_H
#define SC_MOUSE_CAPTURE_H

#include "common.h"

#include <stdbool.h>

#include <SDL2/SDL.h>

struct sc_mouse_capture {
    SDL_Window *window;
    uint16_t sdl_mouse_capture_keys;

    // Mapped game input consumes relative deltas like an FPS and keeps the
    // logical cursor anchored at the window center after every sample. Native
    // scrplay relative input keeps its upstream behavior.
    bool recenter;
    bool saved_relative_mode_center;
    bool saved_relative_warp_motion;

    // To enable/disable mouse capture, a mouse capture key (LALT, LGUI or
    // RGUI) must be pressed. This variable tracks the pressed capture key.
    SDL_Keycode mouse_capture_key_pressed;

};

void
sc_mouse_capture_init(struct sc_mouse_capture *mc, SDL_Window *window,
                      uint8_t shortcut_mods);

void
sc_mouse_capture_set_active(struct sc_mouse_capture *mc, bool capture);

// Enable FPS-style center anchoring. This only configures the policy; callers
// explicitly invoke sc_mouse_capture_recenter() after consuming a real delta.
void
sc_mouse_capture_set_recenter(struct sc_mouse_capture *mc, bool enabled);

// Move an actively captured pointer to the logical window center. SDL is
// configured not to turn this warp into another relative motion event.
void
sc_mouse_capture_recenter(struct sc_mouse_capture *mc);

// Change the SDL key mask used to toggle relative capture. Any in-progress
// capture-key chord is cancelled. Passing 0 disables key-driven toggling.
void
sc_mouse_capture_set_keys(struct sc_mouse_capture *mc,
                          uint16_t sdl_mouse_capture_keys);

bool
sc_mouse_capture_is_active(struct sc_mouse_capture *mc);

void
sc_mouse_capture_toggle(struct sc_mouse_capture *mc);

// Return true if it consumed the event
bool
sc_mouse_capture_handle_event(struct sc_mouse_capture *mc,
                              const SDL_Event *event);

#endif
