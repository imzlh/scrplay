#ifndef SC_GAME_ENGINE_H
#define SC_GAME_ENGINE_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

#include "controller.h"
#include "game_profile.h"
#include "util/thread.h"

// Injects synthetic touch streams for the current preset at a fixed rate.
//
// All state is protected by a single mutex. Mutation entry points are called
// from the main/UI thread; the internal thread only samples the state and
// pushes INJECT_TOUCH_EVENT control messages.
struct sc_game_engine {
    struct sc_controller *controller;

    sc_mutex mutex;
    sc_cond cond;
    sc_thread thread;
    bool thread_started;
    sc_tick deadline; // next tick deadline
    bool stopped;

    // --- protected by mutex ---
    bool active; // inject only when true (play mode)
    bool captured; // mapped play mode currently owns relative mouse input
    struct sc_game_profile profile; // snapshot of the current profile
    bool frame_size_valid;
    struct sc_size frame_size;
    float aim_scale_x; // frame pixels per SDL window unit
    float aim_scale_y;

    // input state
    uint32_t pressed_mouse; // OR of sc_mouse_button values
    // Modifiers held continuously since each physical DOWN. Bits are cleared
    // on modifier UP and never restored until the button/key is pressed anew.
    uint8_t pressed_mouse_mods[32];
#define SC_GAME_MAX_PRESSED_KEYS 32
    uint32_t pressed_keys[SC_GAME_MAX_PRESSED_KEYS]; // sc_keycode (SDL space)
    uint8_t pressed_key_mods[SC_GAME_MAX_PRESSED_KEYS];
    size_t pressed_key_count;
    uint8_t cur_mods; // OR of SC_GAME_MOD_*
    uint32_t rng_state; // engine-local PRNG (never uses global libc state)

    float aim_acc_x;
    float aim_acc_y;

    // runtime per-element state (indexed by element index in current preset)
    bool joy_touch_active;
    float joy_vec_x;
    float joy_vec_y;
    float joy_last_x; // last injected position, normalized (for coherent UPs)
    float joy_last_y;
    // eased thumb slide: the touch glides from its current position to the
    // deflected target (ease-out: fast first, decelerating), like a real
    // thumb -- never a teleport, which anti-cheat can flag
    bool joy_sliding;
    float joy_slide_from_x;
    float joy_slide_from_y;
    float joy_slide_progress; // 0..1 within SC_GAME_JOY_SLIDE_MS
    float joy_tgt_x;          // current deflection target (retarget detect)
    float joy_tgt_y;
    bool aim_touch_active;
    float aim_pos_x; // device pixels
    float aim_pos_y;
    bool bind_pressed[SC_GAME_MAX_ELEMENTS];
    bool bind_touch_active[SC_GAME_MAX_ELEMENTS];
    bool bind_press_pending[SC_GAME_MAX_ELEMENTS]; // sub-tick press edge
    float bind_last_x[SC_GAME_MAX_ELEMENTS]; // normalized
    float bind_last_y[SC_GAME_MAX_ELEMENTS];
    bool bind_tap_pending[SC_GAME_MAX_ELEMENTS];
    int bind_tap_ticks[SC_GAME_MAX_ELEMENTS];
    // swipe gestures (indexed by element index like the bind arrays)
    bool swipe_gesture[SC_GAME_MAX_ELEMENTS];
    int swipe_wait_ticks[SC_GAME_MAX_ELEMENTS];
    float swipe_progress[SC_GAME_MAX_ELEMENTS];
    float swipe_start_x[SC_GAME_MAX_ELEMENTS]; // jittered, normalized
    float swipe_start_y[SC_GAME_MAX_ELEMENTS];
    float swipe_delta_x[SC_GAME_MAX_ELEMENTS];
    float swipe_delta_y[SC_GAME_MAX_ELEMENTS];
    // scripted self-test (SCRPLAY_GAME_SELFTEST), for on-device evidence
    bool selftest;
    int32_t selftest_ms; // may start negative = delay before the timeline
    uint8_t selftest_step;
    // --- end protected ---
};

bool
sc_game_engine_init(struct sc_game_engine *eng,
                    struct sc_controller *controller);

// Start the injection thread. On failure, the engine remains usable for
// state updates but does not inject.
bool
sc_game_engine_start(struct sc_game_engine *eng);

// Stop and join the injection thread (releases all virtual touches).
void
sc_game_engine_stop(struct sc_game_engine *eng);

void
sc_game_engine_destroy(struct sc_game_engine *eng);

// --- called from the main/UI thread ---

// When deactivated, all virtual touches are immediately released.
void
sc_game_engine_set_active(struct sc_game_engine *eng, bool active);

// Notify the engine whether mapped play currently owns captured relative
// mouse input. Enabling capture immediately starts the continuous aim touch
// at its configured start point (when active and ready); disabling capture
// immediately releases every virtual touch. This operation is idempotent.
void
sc_game_engine_set_captured(struct sc_game_engine *eng, bool captured);

void
sc_game_engine_set_frame_size(struct sc_game_engine *eng,
                              struct sc_size frame_size, float aim_scale_x,
                              float aim_scale_y);

// Replace the current profile snapshot (copies the whole struct).
void
sc_game_engine_set_profile(struct sc_game_engine *eng,
                           const struct sc_game_profile *profile);

// Cycle to the next preset (releases all virtual touches first).
void
sc_game_engine_next_preset(struct sc_game_engine *eng);

// key_event_code is the sc_keycode (SDL keycode space) from the key event.
// Returns true if the key is referenced by the current preset (the caller
// must then not forward it to the device).
bool
sc_game_engine_on_key(struct sc_game_engine *eng, uint32_t key_event_code,
                      bool down);

bool
sc_game_engine_on_mouse_button(struct sc_game_engine *eng,
                               uint32_t mouse_button, bool down);

// Accumulate relative mouse motion (SDL units) for the aim pointer.
void
sc_game_engine_add_aim_delta(struct sc_game_engine *eng, float dx, float dy);

// Update the current modifier state (OR of SC_GAME_MOD_*), for combo keys.
void
sc_game_engine_set_mods(struct sc_game_engine *eng, uint8_t mods);

// Force-release all virtual touches (mode change, focus loss, shutdown).
void
sc_game_engine_release_all(struct sc_game_engine *eng);

#ifdef SC_TEST
// Execute exactly one engine tick synchronously, without starting a thread.
void
sc_game_engine_tick_for_test(struct sc_game_engine *eng);
#endif

#endif
