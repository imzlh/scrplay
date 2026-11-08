#ifndef SC_GAME_PROFILE_H
#define SC_GAME_PROFILE_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SC_GAME_PROFILE_NAME_MAX 64
#define SC_GAME_PRESET_NAME_MAX 64
#define SC_GAME_ELEMENT_LABEL_MAX 32
#define SC_GAME_MAX_PRESETS 12
#define SC_GAME_MAX_ELEMENTS 48
#define SC_GAME_PROFILE_FORMAT_VERSION 2

// Canonical value ranges shared by profile parsing, validation and editors.
#define SC_GAME_COORD_MIN 0.00f
#define SC_GAME_COORD_MAX 1.00f
#define SC_GAME_CROP_MIN_SIZE 0.01f
#define SC_GAME_JOYSTICK_RADIUS_MIN 0.001f
#define SC_GAME_JOYSTICK_RADIUS_MAX 0.499f
#define SC_GAME_BIND_SIZE_MIN 0.001f
#define SC_GAME_BIND_SIZE_MAX 0.499f
#define SC_GAME_AIM_SENSITIVITY_MIN 0.10f
#define SC_GAME_AIM_SENSITIVITY_MAX 5.00f
#define SC_GAME_HUMANIZE_AMP_MIN 0.00f
#define SC_GAME_HUMANIZE_AMP_MAX 0.02f
#define SC_GAME_SWIPE_DURATION_MIN 40
#define SC_GAME_SWIPE_DURATION_MAX 5000
#define SC_GAME_SWIPE_PERIOD_MIN 60
#define SC_GAME_SWIPE_PERIOD_MAX 30000

// Virtual pointer ids injected by the game engine. Negative values are not
// used by Android for real fingers; scrplay itself uses -1 (mouse), -2
// (generic finger) and -3 (pinch virtual finger), so start below.
#define SC_GAME_POINTER_ID_JOYSTICK UINT64_C(-4)
#define SC_GAME_POINTER_ID_AIM UINT64_C(-5)
#define SC_GAME_POINTER_ID_BIND_BASE UINT64_C(-10) // actual id: BASE - index

enum sc_game_key_type {
    SC_GAME_KEY_NONE = 0,
    SC_GAME_KEY_KEYBOARD, // value = SDL keycode
    SC_GAME_KEY_MOUSE,    // value = sc_mouse_button (SC_MOUSE_BUTTON_*)
};

// Modifier bits for combo keys (e.g. ctrl+1), abstracted from SDL mods.
#define SC_GAME_MOD_SHIFT 0x1
#define SC_GAME_MOD_CTRL 0x2
#define SC_GAME_MOD_ALT 0x4

struct sc_game_key {
    enum sc_game_key_type type;
    uint32_t value;
    uint8_t mods; // OR of SC_GAME_MOD_*
};

enum sc_game_bind_action {
    SC_GAME_ACTION_TAP, // touch down, ~40ms, touch up
    SC_GAME_ACTION_HOLD, // touch down while key/button is pressed
};

// Coordinates and sizes are normalized to [0.0f, 1.0f] relative to the full
// raw device frame. Client-side cropping never changes existing bindings.
struct sc_game_vec2 {
    float x;
    float y;
};

enum sc_game_element_type {
    SC_GAME_ELEMENT_JOYSTICK,
    SC_GAME_ELEMENT_BIND,
    SC_GAME_ELEMENT_SWIPE,
    SC_GAME_ELEMENT_AIM,
};

struct sc_game_element {
    enum sc_game_element_type type;
    char label[SC_GAME_ELEMENT_LABEL_MAX];
    union {
        struct {
            struct sc_game_vec2 center;
            float radius; // normalized, relative to frame height
            struct sc_game_key up;
            struct sc_game_key down;
            struct sc_game_key left;
            struct sc_game_key right;
        } joystick;
        struct {
            struct sc_game_key key;
            enum sc_game_bind_action action;
            struct sc_game_vec2 center;
            float size; // normalized, relative to frame height
        } bind;
        struct {
            struct sc_game_key key;
            struct sc_game_vec2 from;
            struct sc_game_vec2 to;
            uint32_t dur_ms;  // gesture duration
            uint32_t every_ms; // repeat period while held
        } swipe;
        struct {
            // The fixed touch-down point used to start/restart a continuous
            // camera drag. The engine is free to move the finger anywhere in
            // the safe frame area after the initial DOWN.
            struct sc_game_vec2 start;
        } aim;
    };
};

// Client-side crop in normalized raw-frame coordinates. right/bottom are the
// exclusive edges. The full rectangle remains initialized even when disabled
// so profile snapshots and serialization are deterministic.
struct sc_game_crop {
    bool enabled;
    float left;
    float top;
    float right;
    float bottom;
};

struct sc_game_aim {
    bool enabled;
    float sensitivity;
};

// Humanized input: small random jitter on positions and timings, to mimic
// the imperfection of a real finger.
struct sc_game_humanize {
    bool enabled;
    float amp; // max random position jitter, normalized (e.g. 0.003)
};

struct sc_game_hotkey {
    struct sc_game_key key;
    bool shift;
    bool ctrl;
    bool alt;
};

struct sc_game_preset {
    char name[SC_GAME_PRESET_NAME_MAX];
    struct sc_game_element elements[SC_GAME_MAX_ELEMENTS];
    size_t element_count;
    struct sc_game_aim aim;
    struct sc_game_humanize humanize;
};

struct sc_game_profile {
    char name[SC_GAME_PROFILE_NAME_MAX];
    struct sc_game_preset presets[SC_GAME_MAX_PRESETS];
    size_t preset_count;
    size_t current_preset;

    struct sc_game_hotkey hotkey_edit;      // toggle edit/run mode
    struct sc_game_hotkey hotkey_next_preset; // cycle presets
    struct sc_game_crop crop;
};

// A profile name is also used as a file name. It must be a non-empty,
// whitespace-free token, must not contain path separators or control
// characters, and must not be "." or "..".
bool
sc_game_profile_name_is_valid(const char *name);

// Preset names are serialized as one unquoted token, so whitespace and
// control characters are not permitted.
bool
sc_game_preset_name_is_valid(const char *name);

// Validate the complete in-memory model, including names, counts, keys and
// all numeric ranges. A valid profile can be serialized and parsed back.
bool
sc_game_profile_validate(const struct sc_game_profile *profile);

// Parse a format-versioned profile from text. The first non-empty,
// non-comment line must be exactly "format 2". On failure, return false and
// set *error_line (1-based) if non-NULL.
bool
sc_game_profile_parse(struct sc_game_profile *profile, const char *text,
                      size_t *error_line);

// Serialize a validated profile to a malloc'ed NUL-terminated string (caller
// frees with free()). Returns NULL if validation or allocation fails.
char *
sc_game_profile_serialize(const struct sc_game_profile *profile);

// Fill an empty profile with sensible defaults (one ARPG-oriented preset).
void
sc_game_profile_init_default(struct sc_game_profile *profile,
                             const char *name);

// Key name conversions, used by the parser, the serializer and the UI.
// Names: "a".."z", "0".."9", "f1".."f12", "shift", "ctrl", "alt", "space",
// "tab", "enter", "esc", "up", "down", "left", "right", "lmb", "rmb", "m4",
// "m5", "none".
bool
sc_game_key_from_name(const char *name, struct sc_game_key *key);

void
sc_game_key_to_name(const struct sc_game_key *key, char *out, size_t len);

// Android keycode used by the engine to match keyboard events (0 if the key
// has no Android equivalent or is a mouse button).
uint32_t
sc_game_key_to_android(const struct sc_game_key *key);

// Parse a hotkey description like "shift+e" or "f8". Returns false on error.
bool
sc_game_hotkey_from_name(const char *name, struct sc_game_hotkey *hotkey);

// Serialize a hotkey back to "shift+e" form.
void
sc_game_hotkey_to_name(const struct sc_game_hotkey *hotkey, char *out,
                       size_t len);

#endif
