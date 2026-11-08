#ifndef SC_INPUTMANAGER_H
#define SC_INPUTMANAGER_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>

#include "controller.h"
#include "file_pusher.h"
#include "options.h"
#include "trait/gamepad_processor.h"
#include "trait/key_processor.h"
#include "trait/mouse_processor.h"

#define SC_INPUT_MANAGER_MAX_TOUCHES 10

struct sc_input_manager {
    struct sc_controller *controller;
    struct sc_file_pusher *fp;
    struct sc_screen *screen;

    struct sc_key_processor *kp;
    struct sc_mouse_processor *mp;
    struct sc_gamepad_processor *gp;

    struct sc_mouse_bindings mouse_bindings;
    bool legacy_paste;
    bool clipboard_autosync;
    // Game mode owns keyboard shortcuts and Ctrl combinations itself.
    bool game_mode;

    uint16_t sdl_shortcut_mods;

    bool vfinger_down;
    bool vfinger_invert_x;
    bool vfinger_invert_y;
    struct sc_point vfinger_position;

    // Absolute mouse events outside the displayed video are ignored. Keep
    // ownership and the last injectable position so an UP outside the video
    // can still close a DOWN which started inside it.
    uint8_t absolute_mouse_buttons;
    struct sc_position absolute_mouse_position;

    // SDL touch streams need the same ownership rule, independently for each
    // finger. Ten matches Android's maximum number of injected pointers.
    struct {
        bool active;
        SDL_FingerID id;
        struct sc_position position;
    } touches[SC_INPUT_MANAGER_MAX_TOUCHES];

    // Mouse shortcuts (right click = BACK, middle click = HOME, …) are
    // resolved on DOWN. Keep that ownership until UP so changing between
    // native scrplay controls and game mappings cannot split a press pair.
    enum sc_mouse_binding active_mouse_bindings[SDL_BUTTON_X2 + 1];
    bool released_mouse_bindings[SDL_BUTTON_X2 + 1];

    uint8_t mouse_buttons_state; // OR of enum sc_mouse_button values

    // Tracks the number of identical consecutive shortcut key down events.
    // Not to be confused with event->repeat, which counts the number of
    // system-generated repeated key presses.
    unsigned key_repeat;
    SDL_Keycode last_keycode;
    uint16_t last_mod;

    uint64_t next_sequence; // used for request acknowledgements
};

struct sc_input_manager_params {
    struct sc_controller *controller;
    struct sc_file_pusher *fp;
    struct sc_screen *screen;
    struct sc_key_processor *kp;
    struct sc_mouse_processor *mp;
    struct sc_gamepad_processor *gp;

    struct sc_mouse_bindings mouse_bindings;
    bool legacy_paste;
    bool clipboard_autosync;
    bool game_mode;
    uint8_t shortcut_mods; // OR of enum sc_shortcut_mod values
};

void
sc_input_manager_init(struct sc_input_manager *im,
                      const struct sc_input_manager_params *params);

void
sc_input_manager_handle_event(struct sc_input_manager *im,
                              const SDL_Event *event);

// Enable/disable the game wrapper ownership rules at runtime. Native mode
// restores the regular scrplay shortcuts and clipboard behavior.
void
sc_input_manager_set_game_mode(struct sc_input_manager *im, bool game_mode);

// Release input-manager-owned virtual/special mouse actions and clear its
// physical mouse bookkeeping (focus loss/shutdown). Processor-owned keys and
// buttons are released by their owner, not here.
void
sc_input_manager_release_all(struct sc_input_manager *im);

#endif
