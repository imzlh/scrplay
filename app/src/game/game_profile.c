#include "game_profile.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL_keycode.h>

#include "input_events.h"
#include "android/keycodes.h"

// A named key usable in profiles. "sdl" is used for display and for the
// editor; "android" is used by the engine to match key events.
struct sc_game_key_entry {
    const char *name;
    uint32_t sdl;    // SDL_Keycode, or 0 for mouse buttons
    uint32_t android; // Android keycode, or 0 for mouse buttons
    enum sc_game_key_type type;
    uint32_t mouse; // sc_mouse_button value for mouse entries
};

#define KEYSYM(sdl) ((uint32_t) (sdl))

static const struct sc_game_key_entry sc_game_key_table[] = {
    {"lmb",   0, 0, SC_GAME_KEY_MOUSE, SC_MOUSE_BUTTON_LEFT},
    {"rmb",   0, 0, SC_GAME_KEY_MOUSE, SC_MOUSE_BUTTON_RIGHT},
    {"mmb",   0, 0, SC_GAME_KEY_MOUSE, SC_MOUSE_BUTTON_MIDDLE},
    {"m4",    0, 0, SC_GAME_KEY_MOUSE, SC_MOUSE_BUTTON_X1},
    {"m5",    0, 0, SC_GAME_KEY_MOUSE, SC_MOUSE_BUTTON_X2},
    {"space", KEYSYM(SDLK_SPACE), AKEYCODE_SPACE, SC_GAME_KEY_KEYBOARD, 0},
    {"tab", KEYSYM(SDLK_TAB), AKEYCODE_TAB, SC_GAME_KEY_KEYBOARD, 0},
    {"enter", KEYSYM(SDLK_RETURN), AKEYCODE_ENTER, SC_GAME_KEY_KEYBOARD, 0},
    {"esc", KEYSYM(SDLK_ESCAPE), AKEYCODE_ESCAPE, SC_GAME_KEY_KEYBOARD, 0},
    {"shift", KEYSYM(SDLK_LSHIFT), AKEYCODE_SHIFT_LEFT, SC_GAME_KEY_KEYBOARD, 0},
    {"ctrl", KEYSYM(SDLK_LCTRL), AKEYCODE_CTRL_LEFT, SC_GAME_KEY_KEYBOARD, 0},
    {"alt", KEYSYM(SDLK_LALT), AKEYCODE_ALT_LEFT, SC_GAME_KEY_KEYBOARD, 0},
    {"up", KEYSYM(SDLK_UP), AKEYCODE_DPAD_UP, SC_GAME_KEY_KEYBOARD, 0},
    {"down", KEYSYM(SDLK_DOWN), AKEYCODE_DPAD_DOWN, SC_GAME_KEY_KEYBOARD, 0},
    {"left", KEYSYM(SDLK_LEFT), AKEYCODE_DPAD_LEFT, SC_GAME_KEY_KEYBOARD, 0},
    {"right", KEYSYM(SDLK_RIGHT), AKEYCODE_DPAD_RIGHT, SC_GAME_KEY_KEYBOARD, 0},
};

#define SC_GAME_KEY_TABLE_LEN \
    (sizeof(sc_game_key_table) / sizeof(sc_game_key_table[0]))

static bool
name_is_bounded(const char *name, size_t max_len) {
    return name && name[0] && strnlen(name, max_len) < max_len;
}

bool
sc_game_profile_name_is_valid(const char *name) {
    if (!name_is_bounded(name, SC_GAME_PROFILE_NAME_MAX)
            || !strcmp(name, ".") || !strcmp(name, "..")) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *) name; *p; ++p) {
        if (iscntrl(*p) || isspace(*p) || *p == '/' || *p == '\\') {
            return false;
        }
    }
    return true;
}

bool
sc_game_preset_name_is_valid(const char *name) {
    if (!name_is_bounded(name, SC_GAME_PRESET_NAME_MAX)) {
        return false;
    }

    for (const unsigned char *p = (const unsigned char *) name; *p; ++p) {
        if (iscntrl(*p) || isspace(*p)) {
            return false;
        }
    }
    return true;
}

// Labels are serialized after "label=" and may contain ordinary spaces.
// Reject forms which the whitespace tokenizer would normalize on reparse.
static bool
label_is_valid(const char *label) {
    if (!name_is_bounded(label, SC_GAME_ELEMENT_LABEL_MAX)) {
        return false;
    }

    bool previous_space = true; // also rejects a leading space
    for (const unsigned char *p = (const unsigned char *) label; *p; ++p) {
        if (iscntrl(*p) || (isspace(*p) && *p != ' ')) {
            return false;
        }
        bool space = *p == ' ';
        if (space && previous_space) {
            return false;
        }
        previous_space = space;
    }
    return !previous_space; // rejects a trailing space
}

static bool
float_in_range(float value, float min, float max) {
    return isfinite(value) && value >= min && value <= max;
}

static bool
crop_is_valid(const struct sc_game_crop *crop) {
    bool geometry_valid =
           float_in_range(crop->left, SC_GAME_COORD_MIN, SC_GAME_COORD_MAX)
        && float_in_range(crop->top, SC_GAME_COORD_MIN, SC_GAME_COORD_MAX)
        && float_in_range(crop->right, SC_GAME_COORD_MIN, SC_GAME_COORD_MAX)
        && float_in_range(crop->bottom, SC_GAME_COORD_MIN, SC_GAME_COORD_MAX)
        && crop->right - crop->left >= SC_GAME_CROP_MIN_SIZE
        && crop->bottom - crop->top >= SC_GAME_CROP_MIN_SIZE;
    if (!geometry_valid) {
        return false;
    }
    return crop->enabled
        || (crop->left == 0.0f && crop->top == 0.0f
            && crop->right == 1.0f && crop->bottom == 1.0f);
}

static bool
key_is_valid(const struct sc_game_key *key, bool allow_none) {
    if (key->mods & ~(SC_GAME_MOD_SHIFT | SC_GAME_MOD_CTRL | SC_GAME_MOD_ALT)) {
        return false;
    }
    if (key->type == SC_GAME_KEY_NONE) {
        return allow_none && !key->value && !key->mods;
    }
    if (key->type == SC_GAME_KEY_MOUSE) {
        return key->value == SC_MOUSE_BUTTON_LEFT
            || key->value == SC_MOUSE_BUTTON_RIGHT
            || key->value == SC_MOUSE_BUTTON_MIDDLE
            || key->value == SC_MOUSE_BUTTON_X1
            || key->value == SC_MOUSE_BUTTON_X2;
    }
    if (key->type != SC_GAME_KEY_KEYBOARD) {
        return false;
    }

    uint32_t value = key->value;
    if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9')
            || (value >= KEYSYM(SDLK_F1) && value <= KEYSYM(SDLK_F12))) {
        return true;
    }
    for (size_t i = 0; i < SC_GAME_KEY_TABLE_LEN; ++i) {
        if (sc_game_key_table[i].type == SC_GAME_KEY_KEYBOARD
                && sc_game_key_table[i].sdl == value) {
            return true;
        }
    }
    return false;
}

static bool
hotkey_is_valid(const struct sc_game_hotkey *hotkey) {
    return hotkey->key.type == SC_GAME_KEY_KEYBOARD
        && !hotkey->key.mods && key_is_valid(&hotkey->key, false);
}

static bool
element_is_valid(const struct sc_game_element *elem) {
    if (!label_is_valid(elem->label)) {
        return false;
    }

    switch (elem->type) {
        case SC_GAME_ELEMENT_JOYSTICK:
            return float_in_range(elem->joystick.center.x, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && float_in_range(elem->joystick.center.y, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && float_in_range(elem->joystick.radius,
                                  SC_GAME_JOYSTICK_RADIUS_MIN,
                                  SC_GAME_JOYSTICK_RADIUS_MAX)
                && key_is_valid(&elem->joystick.up, true)
                && key_is_valid(&elem->joystick.down, true)
                && key_is_valid(&elem->joystick.left, true)
                && key_is_valid(&elem->joystick.right, true);
        case SC_GAME_ELEMENT_BIND:
            return key_is_valid(&elem->bind.key, true)
                && (elem->bind.action == SC_GAME_ACTION_TAP
                    || elem->bind.action == SC_GAME_ACTION_HOLD)
                && float_in_range(elem->bind.center.x, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && float_in_range(elem->bind.center.y, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && float_in_range(elem->bind.size, SC_GAME_BIND_SIZE_MIN,
                                  SC_GAME_BIND_SIZE_MAX);
        case SC_GAME_ELEMENT_SWIPE:
            return key_is_valid(&elem->swipe.key, true)
                && float_in_range(elem->swipe.from.x, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && float_in_range(elem->swipe.from.y, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && float_in_range(elem->swipe.to.x, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && float_in_range(elem->swipe.to.y, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && elem->swipe.dur_ms >= SC_GAME_SWIPE_DURATION_MIN
                && elem->swipe.dur_ms <= SC_GAME_SWIPE_DURATION_MAX
                && elem->swipe.every_ms >= SC_GAME_SWIPE_PERIOD_MIN
                && elem->swipe.every_ms <= SC_GAME_SWIPE_PERIOD_MAX;
        case SC_GAME_ELEMENT_AIM:
            return float_in_range(elem->aim.start.x, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX)
                && float_in_range(elem->aim.start.y, SC_GAME_COORD_MIN,
                                  SC_GAME_COORD_MAX);
        default:
            return false;
    }
}

bool
sc_game_profile_validate(const struct sc_game_profile *profile) {
    if (!profile || !sc_game_profile_name_is_valid(profile->name)
            || !profile->preset_count
            || profile->preset_count > SC_GAME_MAX_PRESETS
            || profile->current_preset >= profile->preset_count
            || !hotkey_is_valid(&profile->hotkey_edit)
            || !hotkey_is_valid(&profile->hotkey_next_preset)
            || !crop_is_valid(&profile->crop)) {
        return false;
    }

    for (size_t p = 0; p < profile->preset_count; ++p) {
        const struct sc_game_preset *preset = &profile->presets[p];
        if (!sc_game_preset_name_is_valid(preset->name)
                || preset->element_count > SC_GAME_MAX_ELEMENTS
                || !float_in_range(preset->aim.sensitivity,
                                   SC_GAME_AIM_SENSITIVITY_MIN,
                                   SC_GAME_AIM_SENSITIVITY_MAX)
                || !float_in_range(preset->humanize.amp,
                                   SC_GAME_HUMANIZE_AMP_MIN,
                                   SC_GAME_HUMANIZE_AMP_MAX)) {
            return false;
        }
        for (size_t i = 0; i < preset->element_count; ++i) {
            if (!element_is_valid(&preset->elements[i])) {
                return false;
            }
        }
    }
    return true;
}

// Letters a-z map to SDL keycodes 'a'..'z' and Android AKEYCODE_A..Z (29..54)
// Digits 0-9 map to '0'..'9' and AKEYCODE_0..9 (7..16)
// F1-F12 map to SDLK_F1..SDLK_F12 and AKEYCODE_F1..F12 (131..142)

static bool
sc_game_key_lookup_letter(char c, struct sc_game_key *key) {
    if (c >= 'a' && c <= 'z') {
        key->type = SC_GAME_KEY_KEYBOARD;
        key->value = KEYSYM('a' + (c - 'a'));
        return true;
    }
    return false;
}

static bool
sc_game_key_lookup_digit(char c, struct sc_game_key *key) {
    if (c >= '0' && c <= '9') {
        key->type = SC_GAME_KEY_KEYBOARD;
        key->value = KEYSYM('0' + (c - '0'));
        return true;
    }
    return false;
}

static bool
sc_game_key_lookup_fkey(const char *name, struct sc_game_key *key) {
    if (name[0] != 'f' && name[0] != 'F') {
        return false;
    }
    char *end;
    long n = strtol(name + 1, &end, 10);
    if (end == name + 1 || *end != '\0' || n < 1 || n > 12) {
        return false;
    }
    static const uint32_t sdl_fkeys[] = {
        KEYSYM(SDLK_F1), KEYSYM(SDLK_F2), KEYSYM(SDLK_F3), KEYSYM(SDLK_F4),
        KEYSYM(SDLK_F5), KEYSYM(SDLK_F6), KEYSYM(SDLK_F7), KEYSYM(SDLK_F8),
        KEYSYM(SDLK_F9), KEYSYM(SDLK_F10), KEYSYM(SDLK_F11), KEYSYM(SDLK_F12),
    };
    key->type = SC_GAME_KEY_KEYBOARD;
    key->value = sdl_fkeys[n - 1];
    return true;
}

static bool
sc_game_key_from_single(const char *name, struct sc_game_key *key) {
    if (!name[0]) {
        return false;
    }
    if (name[1] == '\0') {
        if (sc_game_key_lookup_letter(name[0], key)
                || sc_game_key_lookup_digit(name[0], key)) {
            return true;
        }
    }
    if (sc_game_key_lookup_fkey(name, key)) {
        return true;
    }
    for (size_t i = 0; i < SC_GAME_KEY_TABLE_LEN; ++i) {
        if (!strcmp(name, sc_game_key_table[i].name)) {
            key->type = sc_game_key_table[i].type;
            if (key->type == SC_GAME_KEY_MOUSE) {
                key->value = sc_game_key_table[i].mouse;
            } else {
                key->value = sc_game_key_table[i].sdl;
            }
            return true;
        }
    }
    return false;
}

bool
sc_game_key_from_name(const char *name, struct sc_game_key *key) {
    assert(name);
    assert(key);

    key->type = SC_GAME_KEY_NONE;
    key->value = 0;
    key->mods = 0;

    if (!strcmp(name, "none")) {
        return true;
    }

    // "shift" alone means the shift KEY itself; modifiers only act as
    // modifiers when combined with a base key via '+'
    if (!strchr(name, '+')) {
        return sc_game_key_from_single(name, key);
    }

    // support combos: "ctrl+shift+1"
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", name);
    char *saveptr = NULL;
    bool has_base = false;
    for (char *tok = strtok_r(buf, "+", &saveptr); tok;
            tok = strtok_r(NULL, "+", &saveptr)) {
        if (!strcmp(tok, "shift")) {
            key->mods |= SC_GAME_MOD_SHIFT;
        } else if (!strcmp(tok, "ctrl")) {
            key->mods |= SC_GAME_MOD_CTRL;
        } else if (!strcmp(tok, "alt")) {
            key->mods |= SC_GAME_MOD_ALT;
        } else {
            if (has_base || !sc_game_key_from_single(tok, key)) {
                return false;
            }
            has_base = true;
        }
    }
    return has_base;
}

void
sc_game_key_to_name(const struct sc_game_key *key, char *out, size_t len) {
    assert(key);
    assert(out);
    assert(len);

    if (key->type == SC_GAME_KEY_NONE) {
        snprintf(out, len, "none");
        return;
    }
    char base[32];
    base[0] = '\0';
    const char *base_name = NULL;
    char letter_buf[2] = {0, 0};
    for (size_t i = 0; i < SC_GAME_KEY_TABLE_LEN; ++i) {
        const struct sc_game_key_entry *e = &sc_game_key_table[i];
        uint32_t value = (key->type == SC_GAME_KEY_MOUSE) ? e->mouse : e->sdl;
        if (key->type == e->type && key->value == value) {
            base_name = e->name;
            break;
        }
    }
    if (!base_name && key->type == SC_GAME_KEY_KEYBOARD) {
        uint32_t v = key->value;
        if ((v >= 'a' && v <= 'z') || (v >= '0' && v <= '9')) {
            letter_buf[0] = (char) v;
            base_name = letter_buf;
        } else if (v >= KEYSYM(SDLK_F1) && v <= KEYSYM(SDLK_F12)) {
            snprintf(base, sizeof(base), "f%u",
                     (unsigned) (v - KEYSYM(SDLK_F1) + 1));
            base_name = base;
        }
    }
    if (!base_name) {
        snprintf(out, len, "?%u", (unsigned) key->value);
        return;
    }
    snprintf(out, len, "%s%s%s%s",
             (key->mods & SC_GAME_MOD_CTRL) ? "ctrl+" : "",
             (key->mods & SC_GAME_MOD_SHIFT) ? "shift+" : "",
             (key->mods & SC_GAME_MOD_ALT) ? "alt+" : "", base_name);
}

uint32_t
sc_game_key_to_android(const struct sc_game_key *key) {
    assert(key);

    if (key->type != SC_GAME_KEY_KEYBOARD) {
        return 0;
    }
    for (size_t i = 0; i < SC_GAME_KEY_TABLE_LEN; ++i) {
        const struct sc_game_key_entry *e = &sc_game_key_table[i];
        if (e->type == SC_GAME_KEY_KEYBOARD && e->sdl == key->value) {
            return e->android;
        }
    }
    uint32_t v = key->value;
    if (v >= 'a' && v <= 'z') {
        return AKEYCODE_A + (v - 'a');
    }
    if (v >= '0' && v <= '9') {
        return AKEYCODE_0 + (v - '0');
    }
    if (v >= KEYSYM(SDLK_F1) && v <= KEYSYM(SDLK_F12)) {
        return AKEYCODE_F1 + (v - KEYSYM(SDLK_F1));
    }
    return 0;
}

bool
sc_game_hotkey_from_name(const char *name, struct sc_game_hotkey *hotkey) {
    assert(name);
    assert(hotkey);

    hotkey->key.type = SC_GAME_KEY_NONE;
    hotkey->key.value = 0;
    hotkey->shift = false;
    hotkey->ctrl = false;
    hotkey->alt = false;

    char buf[64];
    snprintf(buf, sizeof(buf), "%s", name);
    char *saveptr = NULL;
    bool has_key = false;
    for (char *tok = strtok_r(buf, "+", &saveptr); tok;
            tok = strtok_r(NULL, "+", &saveptr)) {
        if (!strcmp(tok, "shift")) {
            hotkey->shift = true;
        } else if (!strcmp(tok, "ctrl")) {
            hotkey->ctrl = true;
        } else if (!strcmp(tok, "alt")) {
            hotkey->alt = true;
        } else {
            if (has_key || !sc_game_key_from_name(tok, &hotkey->key)
                    || hotkey->key.type != SC_GAME_KEY_KEYBOARD) {
                return false;
            }
            has_key = true;
        }
    }
    return has_key;
}

void
sc_game_hotkey_to_name(const struct sc_game_hotkey *hotkey, char *out,
                       size_t len) {
    assert(hotkey);
    assert(out);
    assert(len);

    char key_name[32];
    sc_game_key_to_name(&hotkey->key, key_name, sizeof(key_name));
    snprintf(out, len, "%s%s%s%s", hotkey->shift ? "shift+" : "",
             hotkey->ctrl ? "ctrl+" : "", hotkey->alt ? "alt+" : "", key_name);
}

static char *
trim(char *s) {
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    size_t len = strlen(s);
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t'
                   || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
    return s;
}

// label= value may contain spaces: it consumes the remaining tokens.
static bool
parse_label(char **tokens, size_t argc, size_t *i, char *out, size_t len) {
    const char *first = tokens[*i] + 6; // skip "label="
    int written = snprintf(out, len, "%s", first);
    if (written < 0 || (size_t) written >= len) {
        return false;
    }
    for (size_t j = *i + 1; j < argc; ++j) {
        size_t cur = strlen(out);
        written = snprintf(out + cur, len - cur, " %s", tokens[j]);
        if (written < 0 || (size_t) written >= len - cur) {
            return false;
        }
    }
    *i = argc; // consume the rest
    return label_is_valid(out);
}

static bool
parse_float(const char *s, float *value) {
    if (!s || !*s) {
        return false;
    }
    errno = 0;
    char *end;
    float parsed = strtof(s, &end);
    if (errno == ERANGE || end == s || *end || !isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool
parse_u32(const char *s, uint32_t *value) {
    if (!s || !*s || !isdigit((unsigned char) *s)) {
        return false;
    }
    errno = 0;
    char *end;
    unsigned long long parsed = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s || *end || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t) parsed;
    return true;
}

static bool
parse_vec2(const char *s, struct sc_game_vec2 *vec) {
    if (!s || !*s) {
        return false;
    }
    errno = 0;
    char *comma;
    float x = strtof(s, &comma);
    if (errno == ERANGE || comma == s || *comma != ',' || !isfinite(x)) {
        return false;
    }
    const char *second = comma + 1;
    errno = 0;
    char *end;
    float y = strtof(second, &end);
    if (errno == ERANGE || end == second || *end || !isfinite(y)
            || !float_in_range(x, SC_GAME_COORD_MIN, SC_GAME_COORD_MAX)
            || !float_in_range(y, SC_GAME_COORD_MIN, SC_GAME_COORD_MAX)) {
        return false;
    }
    vec->x = x;
    vec->y = y;
    return true;
}

static bool
parse_crop_rect(const char *s, struct sc_game_crop *crop) {
    if (!s || !*s) {
        return false;
    }

    float values[4];
    const char *next = s;
    for (size_t i = 0; i < ARRAY_LEN(values); ++i) {
        errno = 0;
        char *end;
        values[i] = strtof(next, &end);
        if (errno == ERANGE || end == next || !isfinite(values[i])) {
            return false;
        }
        if (i + 1 < ARRAY_LEN(values)) {
            if (*end != ',') {
                return false;
            }
            next = end + 1;
        } else if (*end) {
            return false;
        }
    }

    crop->enabled = true;
    crop->left = values[0];
    crop->top = values[1];
    crop->right = values[2];
    crop->bottom = values[3];
    return crop_is_valid(crop);
}

static bool
parse_key_param(const char *s, struct sc_game_key *key) {
    return sc_game_key_from_name(s, key);
}

// Parse one space-separated token of the form "name=value". Returns false if
// it does not match the requested name.
static bool
param(const char *token, const char *name, const char **value) {
    size_t n = strlen(name);
    if (strncmp(token, name, n)) {
        return false;
    }
    if (token[n] != '=') {
        return false;
    }
    *value = token + n + 1;
    return true;
}

static bool
parse_joystick(char **tokens, size_t argc, struct sc_game_element *elem) {
    elem->type = SC_GAME_ELEMENT_JOYSTICK;
    elem->joystick.radius = 0.075f;
    elem->joystick.up.type = SC_GAME_KEY_NONE;
    elem->joystick.down.type = SC_GAME_KEY_NONE;
    elem->joystick.left.type = SC_GAME_KEY_NONE;
    elem->joystick.right.type = SC_GAME_KEY_NONE;
    bool has_center = false;

    bool ok = true;
    for (size_t i = 0; i < argc && ok; ++i) {
        const char *value;
        if (param(tokens[i], "center", &value)) {
            ok = parse_vec2(value, &elem->joystick.center);
            has_center = true;
        } else if (param(tokens[i], "radius", &value)) {
            ok = parse_float(value, &elem->joystick.radius)
              && float_in_range(elem->joystick.radius,
                                SC_GAME_JOYSTICK_RADIUS_MIN,
                                SC_GAME_JOYSTICK_RADIUS_MAX);
        } else if (param(tokens[i], "up", &value)) {
            ok = parse_key_param(value, &elem->joystick.up);
        } else if (param(tokens[i], "down", &value)) {
            ok = parse_key_param(value, &elem->joystick.down);
        } else if (param(tokens[i], "left", &value)) {
            ok = parse_key_param(value, &elem->joystick.left);
        } else if (param(tokens[i], "right", &value)) {
            ok = parse_key_param(value, &elem->joystick.right);
        } else if (param(tokens[i], "label", &value)) {
            (void) value;
            ok = parse_label(tokens, argc, &i, elem->label,
                             sizeof(elem->label));
        } else {
            ok = false;
        }
    }
    return ok && has_center;
}

static bool
parse_bind(char **tokens, size_t argc, struct sc_game_element *elem) {
    elem->type = SC_GAME_ELEMENT_BIND;
    elem->bind.action = SC_GAME_ACTION_TAP;
    elem->bind.size = 0.08f;
    elem->bind.key.type = SC_GAME_KEY_NONE;
    bool has_pos = false;
    bool has_key = false;

    bool ok = true;
    for (size_t i = 0; i < argc && ok; ++i) {
        const char *value;
        if (param(tokens[i], "key", &value)) {
            ok = parse_key_param(value, &elem->bind.key);
            has_key = true;
        } else if (param(tokens[i], "action", &value)) {
            if (!strcmp(value, "tap")) {
                elem->bind.action = SC_GAME_ACTION_TAP;
            } else if (!strcmp(value, "hold")) {
                elem->bind.action = SC_GAME_ACTION_HOLD;
            } else {
                ok = false;
            }
        } else if (param(tokens[i], "pos", &value)) {
            ok = parse_vec2(value, &elem->bind.center);
            has_pos = true;
        } else if (param(tokens[i], "size", &value)) {
            ok = parse_float(value, &elem->bind.size)
              && float_in_range(elem->bind.size, SC_GAME_BIND_SIZE_MIN,
                                SC_GAME_BIND_SIZE_MAX);
        } else if (param(tokens[i], "label", &value)) {
            (void) value;
            ok = parse_label(tokens, argc, &i, elem->label,
                             sizeof(elem->label));
        } else {
            ok = false;
        }
    }
    return ok && has_pos && has_key;
}

static bool
parse_swipe(char **tokens, size_t argc, struct sc_game_element *elem) {
    elem->type = SC_GAME_ELEMENT_SWIPE;
    elem->swipe.key.type = SC_GAME_KEY_NONE;
    elem->swipe.dur_ms = 280;
    elem->swipe.every_ms = 500;
    bool has_from = false;
    bool has_to = false;
    bool has_key = false;

    bool ok = true;
    for (size_t i = 0; i < argc && ok; ++i) {
        const char *value;
        if (param(tokens[i], "key", &value)) {
            ok = parse_key_param(value, &elem->swipe.key);
            has_key = true;
        } else if (param(tokens[i], "from", &value)) {
            ok = parse_vec2(value, &elem->swipe.from);
            has_from = true;
        } else if (param(tokens[i], "to", &value)) {
            ok = parse_vec2(value, &elem->swipe.to);
            has_to = true;
        } else if (param(tokens[i], "dur", &value)) {
            ok = parse_u32(value, &elem->swipe.dur_ms)
              && elem->swipe.dur_ms >= SC_GAME_SWIPE_DURATION_MIN
              && elem->swipe.dur_ms <= SC_GAME_SWIPE_DURATION_MAX;
        } else if (param(tokens[i], "every", &value)) {
            ok = parse_u32(value, &elem->swipe.every_ms)
              && elem->swipe.every_ms >= SC_GAME_SWIPE_PERIOD_MIN
              && elem->swipe.every_ms <= SC_GAME_SWIPE_PERIOD_MAX;
        } else if (param(tokens[i], "label", &value)) {
            (void) value;
            ok = parse_label(tokens, argc, &i, elem->label,
                             sizeof(elem->label));
        } else {
            ok = false;
        }
    }
    return ok && has_from && has_to && has_key;
}

// aim start=0.60,0.42 label=瞄准起点
static bool
parse_aim(char **tokens, size_t argc, struct sc_game_element *elem) {
    elem->type = SC_GAME_ELEMENT_AIM;
    bool has_start = false;

    bool ok = true;
    for (size_t i = 0; i < argc && ok; ++i) {
        const char *value;
        if (param(tokens[i], "start", &value)) {
            // Repeated fields are rejected so there is exactly one source of
            // truth for the initial touch point.
            ok = !has_start && parse_vec2(value, &elem->aim.start);
            has_start = true;
        } else if (param(tokens[i], "label", &value)) {
            (void) value;
            ok = parse_label(tokens, argc, &i, elem->label,
                             sizeof(elem->label));
        } else {
            ok = false;
        }
    }
    return ok && has_start;
}

// Split the remainder of a line into in-place tokens; returns token count.
static size_t
split_tokens(char *s, char **tokens, size_t max) {
    size_t count = 0;
    char *saveptr = NULL;
    for (char *tok = strtok_r(s, " \t", &saveptr); tok && count < max;
            tok = strtok_r(NULL, " \t", &saveptr)) {
        tokens[count++] = tok;
    }
    return count;
}

bool
sc_game_profile_parse(struct sc_game_profile *profile, const char *text,
                      size_t *error_line) {
    assert(profile);
    assert(text);

    memset(profile, 0, sizeof(*profile));
    snprintf(profile->name, sizeof(profile->name), "game");
    profile->crop.right = 1.0f;
    profile->crop.bottom = 1.0f;

    struct sc_game_preset *preset = NULL;
    bool has_format = false;
    bool has_name = false;
    bool has_crop = false;

    char *dup = strdup(text);
    if (!dup) {
        return false;
    }

    bool ok = true;
    size_t line_no = 0;
    // Walk physical lines manually. strtok_r() skips empty tokens, which made
    // parse errors after blank lines report the wrong source line.
    char *next_line = dup;
    while (next_line && *next_line) {
        char *line = next_line;
        char *newline = strchr(line, '\n');
        if (newline) {
            *newline = '\0';
            next_line = newline + 1;
        } else {
            next_line = NULL;
        }
        ++line_no;
        char *s = trim(line);
        if (!*s || *s == '#') {
            continue;
        }

        // split command and args
        char *args_start = s;
        while (*args_start && *args_start != ' ' && *args_start != '\t') {
            ++args_start;
        }
        if (*args_start) {
            *args_start++ = '\0';
            args_start = trim(args_start);
        }

        char *tokens[16];
        size_t argc = split_tokens(args_start, tokens, 16);

        // The version marker is the schema boundary. It must be the first
        // effective line, and only the one current spelling is accepted.
        if (!has_format) {
            if (strcmp(s, "format") || argc != 1
                    || strcmp(tokens[0], "2")) {
                ok = false;
                break;
            }
            has_format = true;
            continue;
        }

        if (!strcmp(s, "profile")) {
            if (argc != 1 || has_name
                    || !sc_game_profile_name_is_valid(tokens[0])) {
                ok = false;
                break;
            }
            snprintf(profile->name, sizeof(profile->name), "%s", tokens[0]);
            has_name = true;
        } else if (!strcmp(s, "crop")) {
            if (has_crop || preset || argc != 1) {
                ok = false;
                break;
            }
            if (!strcmp(tokens[0], "off")) {
                profile->crop.enabled = false;
                profile->crop.left = 0.0f;
                profile->crop.top = 0.0f;
                profile->crop.right = 1.0f;
                profile->crop.bottom = 1.0f;
                ok = true;
            } else {
                const char *value;
                ok = param(tokens[0], "rect", &value)
                  && parse_crop_rect(value, &profile->crop);
            }
            has_crop = ok;
        } else if (!strcmp(s, "preset")) {
            if (argc != 1 || !sc_game_preset_name_is_valid(tokens[0])
                    || profile->preset_count >= SC_GAME_MAX_PRESETS) {
                ok = false;
                break;
            }
            preset = &profile->presets[profile->preset_count++];
            snprintf(preset->name, sizeof(preset->name), "%s", tokens[0]);
            preset->aim.enabled = false;
            preset->aim.sensitivity = 1.0f;
            preset->humanize.enabled = true;
            preset->humanize.amp = 0.003f;
            preset->element_count = 0;
        } else if (!strcmp(s, "joystick") || !strcmp(s, "bind")
                || !strcmp(s, "swipe")
                // "aim start=..." is the aim-point ELEMENT; "aim on|off"
                // (handled further below) is the preset-level switch, so
                // only route element lines by their start= param
                || (!strcmp(s, "aim") && argc >= 1
                    && !strncmp(tokens[0], "start=", 6))) {
            if (!preset || preset->element_count >= SC_GAME_MAX_ELEMENTS) {
                ok = false;
                break;
            }
            struct sc_game_element *elem =
                &preset->elements[preset->element_count];
            elem->label[0] = '\0';
            if (!strcmp(s, "joystick")) {
                ok = parse_joystick(tokens, argc, elem);
            } else if (!strcmp(s, "bind")) {
                ok = parse_bind(tokens, argc, elem);
            } else if (!strcmp(s, "aim")) {
                ok = parse_aim(tokens, argc, elem);
            } else {
                ok = parse_swipe(tokens, argc, elem);
            }
            if (ok) {
                if (!elem->label[0]) {
                    if (elem->type == SC_GAME_ELEMENT_AIM) {
                        snprintf(elem->label, sizeof(elem->label), "瞄准起点");
                    } else {
                        char key_name[32];
                        const struct sc_game_key *k =
                            elem->type == SC_GAME_ELEMENT_JOYSTICK
                                ? &elem->joystick.up
                                : (elem->type == SC_GAME_ELEMENT_SWIPE
                                       ? &elem->swipe.key
                                       : &elem->bind.key);
                        sc_game_key_to_name(k, key_name, sizeof(key_name));
                        snprintf(elem->label, sizeof(elem->label), "%s",
                                 key_name);
                    }
                }
                ++preset->element_count;
            }
        } else if (!strcmp(s, "humanize")) {
            if (!preset || argc < 1) {
                ok = false;
                break;
            }
            preset->humanize.enabled = true;
            preset->humanize.amp = 0.003f;
            if (!strcmp(tokens[0], "on")) {
                preset->humanize.enabled = true;
            } else if (!strcmp(tokens[0], "off")) {
                preset->humanize.enabled = false;
            } else {
                ok = false;
                break;
            }
            for (size_t i = 1; i < argc && ok; ++i) {
                const char *value;
                if (param(tokens[i], "amp", &value)) {
                    ok = parse_float(value, &preset->humanize.amp)
                      && float_in_range(preset->humanize.amp,
                                        SC_GAME_HUMANIZE_AMP_MIN,
                                        SC_GAME_HUMANIZE_AMP_MAX);
                } else {
                    ok = false;
                }
            }
        } else if (!strcmp(s, "aim")) {
            if (!preset || argc < 1) {
                ok = false;
                break;
            }
            if (!strcmp(tokens[0], "on")) {
                preset->aim.enabled = true;
            } else if (!strcmp(tokens[0], "off")) {
                preset->aim.enabled = false;
            } else {
                ok = false;
                break;
            }
            for (size_t i = 1; i < argc && ok; ++i) {
                const char *value;
                if (param(tokens[i], "sens", &value)) {
                    ok = parse_float(value, &preset->aim.sensitivity)
                      && float_in_range(preset->aim.sensitivity,
                                        SC_GAME_AIM_SENSITIVITY_MIN,
                                        SC_GAME_AIM_SENSITIVITY_MAX);
                } else {
                    ok = false;
                }
            }
        } else if (!strcmp(s, "hotkey")) {
            for (size_t i = 0; i < argc && ok; ++i) {
                const char *value;
                if (param(tokens[i], "edit", &value)) {
                    ok = sc_game_hotkey_from_name(value,
                                                  &profile->hotkey_edit);
                } else if (param(tokens[i], "preset", &value)) {
                    ok = sc_game_hotkey_from_name(value,
                                              &profile->hotkey_next_preset);
                } else {
                    ok = false;
                }
            }
        } else {
            ok = false;
        }
        if (!ok) {
            break;
        }
    }

    free(dup);

    if (ok) {
        // A parsed profile always starts on its first preset. Validate the
        // whole model so parser and serializer enforce exactly the same rules.
        profile->current_preset = 0;
        ok = has_format && has_name && has_crop && preset
          && sc_game_profile_validate(profile);
    }
    if (!ok) {
        if (error_line) {
            *error_line = line_no ? line_no : 1;
        }
        return false;
    }
    return true;
}

static void
serialize_element(const struct sc_game_element *elem, char *buf, size_t len) {
    if (elem->type == SC_GAME_ELEMENT_SWIPE) {
        char key_name[48];
        sc_game_key_to_name(&elem->swipe.key, key_name, sizeof(key_name));
        snprintf(buf, len,
                 "swipe key=%s from=%.4f,%.4f to=%.4f,%.4f dur=%u every=%u"
                 " label=%s",
                 key_name, elem->swipe.from.x, elem->swipe.from.y,
                 elem->swipe.to.x, elem->swipe.to.y, elem->swipe.dur_ms,
                 elem->swipe.every_ms, elem->label);
    } else if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
        char up[32] = "none";
        char down[32] = "none";
        char left[32] = "none";
        char right[32] = "none";
        if (elem->joystick.up.type != SC_GAME_KEY_NONE) {
            sc_game_key_to_name(&elem->joystick.up, up, sizeof(up));
        }
        if (elem->joystick.down.type != SC_GAME_KEY_NONE) {
            sc_game_key_to_name(&elem->joystick.down, down, sizeof(down));
        }
        if (elem->joystick.left.type != SC_GAME_KEY_NONE) {
            sc_game_key_to_name(&elem->joystick.left, left, sizeof(left));
        }
        if (elem->joystick.right.type != SC_GAME_KEY_NONE) {
            sc_game_key_to_name(&elem->joystick.right, right, sizeof(right));
        }
        snprintf(buf, len,
                 "joystick center=%.4f,%.4f radius=%.4f up=%s down=%s "
                 "left=%s right=%s label=%s",
                 elem->joystick.center.x, elem->joystick.center.y,
                 elem->joystick.radius, up, down, left, right, elem->label);
    } else if (elem->type == SC_GAME_ELEMENT_AIM) {
        snprintf(buf, len, "aim start=%.4f,%.4f label=%s",
                 elem->aim.start.x, elem->aim.start.y, elem->label);
    } else {
        char key_name[32];
        sc_game_key_to_name(&elem->bind.key, key_name, sizeof(key_name));
        snprintf(buf, len,
                 "bind key=%s action=%s pos=%.4f,%.4f size=%.4f label=%s",
                 key_name,
                 elem->bind.action == SC_GAME_ACTION_TAP ? "tap" : "hold",
                 elem->bind.center.x, elem->bind.center.y, elem->bind.size,
                 elem->label);
    }
}

char *
sc_game_profile_serialize(const struct sc_game_profile *profile) {
    assert(profile);

    if (!sc_game_profile_validate(profile)) {
        return NULL;
    }

    // upper bound: generous line buffer per element plus headers
    size_t cap = 4096
               + profile->preset_count
                   * (SC_GAME_MAX_ELEMENTS * 256 + 128);
    char *buf = malloc(cap);
    if (!buf) {
        return NULL;
    }
    size_t pos = 0;
#define APPEND(...) \
    do { \
        int n = snprintf(buf + pos, cap - pos, __VA_ARGS__); \
        if (n < 0 || (size_t) n >= cap - pos) { goto overflow; } \
        pos += (size_t) n; \
    } while (0)

    APPEND("format %d\n", SC_GAME_PROFILE_FORMAT_VERSION);
    APPEND("# scrplay 游戏模式配置\n");
    APPEND("# 坐标均为 0~1 归一化（相对完整原始画面）\n");
    APPEND("profile %s\n", profile->name);

    char edit_name[64];
    char preset_name[64];
    sc_game_hotkey_to_name(&profile->hotkey_edit, edit_name,
                           sizeof(edit_name));
    sc_game_hotkey_to_name(&profile->hotkey_next_preset, preset_name,
                           sizeof(preset_name));
    APPEND("hotkey edit=%s preset=%s\n", edit_name, preset_name);
    if (profile->crop.enabled) {
        APPEND("crop rect=%.6f,%.6f,%.6f,%.6f\n",
               profile->crop.left, profile->crop.top,
               profile->crop.right, profile->crop.bottom);
    } else {
        APPEND("crop off\n");
    }

    for (size_t p = 0; p < profile->preset_count; ++p) {
        const struct sc_game_preset *preset = &profile->presets[p];
        APPEND("\npreset %s\n", preset->name);
        APPEND("aim %s sens=%.2f\n", preset->aim.enabled ? "on" : "off",
               preset->aim.sensitivity);
        APPEND("humanize %s amp=%.4f\n",
               preset->humanize.enabled ? "on" : "off",
               preset->humanize.amp);
        for (size_t i = 0; i < preset->element_count; ++i) {
            char line[512];
            serialize_element(&preset->elements[i], line, sizeof(line));
            APPEND("%s\n", line);
        }
    }

#undef APPEND
    return buf;

overflow:
    free(buf);
    return NULL;
}

void
sc_game_profile_init_default(struct sc_game_profile *profile,
                             const char *name) {
    memset(profile, 0, sizeof(*profile));
    snprintf(profile->name, sizeof(profile->name), "%s", name);
    profile->crop.enabled = false;
    profile->crop.left = 0.0f;
    profile->crop.top = 0.0f;
    profile->crop.right = 1.0f;
    profile->crop.bottom = 1.0f;

    profile->hotkey_edit.key.type = SC_GAME_KEY_KEYBOARD;
    profile->hotkey_edit.key.value = 'e';
    profile->hotkey_edit.shift = true;
    profile->hotkey_next_preset.key.type = SC_GAME_KEY_KEYBOARD;
    profile->hotkey_next_preset.key.value = KEYSYM(SDLK_F8);

    struct sc_game_preset *preset = &profile->presets[0];
    snprintf(preset->name, sizeof(preset->name), "默认");
    preset->aim.enabled = true;
    preset->aim.sensitivity = 1.0f;
    preset->humanize.enabled = true;
    preset->humanize.amp = 0.003f;
    preset->element_count = 10; // 1 joystick + 8 binds + 1 aim start point
    profile->preset_count = 1;

    // second preset for turn-based games (no joystick): click-to-move with a
    // physical repeated swipe, plus a couple of action binds
    struct sc_game_preset *tb = &profile->presets[1];
    snprintf(tb->name, sizeof(tb->name), "回合制(滑动移动)");
    // no camera control in turn-based menus; a resting aim finger would only
    // get in the way of tapping the UI
    tb->aim.enabled = false;
    tb->aim.sensitivity = 1.0f;
    tb->humanize.enabled = true;
    tb->humanize.amp = 0.004f;
    tb->element_count = 3;
    struct sc_game_element *te = tb->elements;
    te[0].type = SC_GAME_ELEMENT_SWIPE;
    snprintf(te[0].label, sizeof(te[0].label), "移动");
    sc_game_key_from_name("w", &te[0].swipe.key);
    te[0].swipe.from.x = 0.50f;
    te[0].swipe.from.y = 0.62f;
    te[0].swipe.to.x = 0.50f;
    te[0].swipe.to.y = 0.42f;
    te[0].swipe.dur_ms = 280;
    te[0].swipe.every_ms = 520;
    te[1].type = SC_GAME_ELEMENT_BIND;
    snprintf(te[1].label, sizeof(te[1].label), "普攻");
    sc_game_key_from_name("lmb", &te[1].bind.key);
    te[1].bind.action = SC_GAME_ACTION_TAP;
    te[1].bind.center.x = 0.88f;
    te[1].bind.center.y = 0.72f;
    te[1].bind.size = 0.10f;
    te[2].type = SC_GAME_ELEMENT_BIND;
    snprintf(te[2].label, sizeof(te[2].label), "技能1");
    sc_game_key_from_name("ctrl+1", &te[2].bind.key);
    te[2].bind.action = SC_GAME_ACTION_TAP;
    te[2].bind.center.x = 0.62f;
    te[2].bind.center.y = 0.80f;
    te[2].bind.size = 0.07f;
    profile->preset_count = 2;

    struct sc_game_element *e = preset->elements;
    size_t i = 0;

    e[i].type = SC_GAME_ELEMENT_JOYSTICK;
    snprintf(e[i].label, sizeof(e[i].label), "摇杆");
    // measured on the cloud HSR overworld (2160x1080 frame):
    // joystick base ring center at (370, 839)
    e[i].joystick.center.x = 0.171f;
    e[i].joystick.center.y = 0.777f;
    e[i].joystick.radius = 0.100f;
    e[i].joystick.up.type = SC_GAME_KEY_KEYBOARD;
    e[i].joystick.up.value = 'w';
    e[i].joystick.down.type = SC_GAME_KEY_KEYBOARD;
    e[i].joystick.down.value = 's';
    e[i].joystick.left.type = SC_GAME_KEY_KEYBOARD;
    e[i].joystick.left.value = 'a';
    e[i].joystick.right.type = SC_GAME_KEY_KEYBOARD;
    e[i].joystick.right.value = 'd';
    ++i;

    static const struct {
        const char *key;
        const char *label;
        enum sc_game_bind_action action;
        float x;
        float y;
        float size;
    } binds[] = {
        // 普攻/技能/冲刺 measured on the cloud HSR overworld (2160x1080);
        // 爆发 is dim when uncharged -- estimated, drag it in the editor
        {"lmb", "普攻", SC_GAME_ACTION_TAP, 0.850f, 0.752f, 0.080f},
        {"rmb", "重击", SC_GAME_ACTION_HOLD, 0.850f, 0.752f, 0.080f},
        {"e", "技能", SC_GAME_ACTION_TAP, 0.760f, 0.852f, 0.065f},
        {"q", "爆发", SC_GAME_ACTION_TAP, 0.800f, 0.700f, 0.055f},
        {"shift", "冲刺", SC_GAME_ACTION_HOLD, 0.931f, 0.852f, 0.060f},
        {"space", "跳跃", SC_GAME_ACTION_TAP, 0.600f, 0.905f, 0.05f},
        {"f", "交互", SC_GAME_ACTION_TAP, 0.500f, 0.760f, 0.05f},
        {"tab", "地图", SC_GAME_ACTION_TAP, 0.950f, 0.070f, 0.045f},
    };
    for (size_t b = 0; b < 8; ++b, ++i) {
        e[i].type = SC_GAME_ELEMENT_BIND;
        snprintf(e[i].label, sizeof(e[i].label), "%s", binds[b].label);
        sc_game_key_from_name(binds[b].key, &e[i].bind.key);
        e[i].bind.action = binds[b].action;
        e[i].bind.center.x = binds[b].x;
        e[i].bind.center.y = binds[b].y;
        e[i].bind.size = binds[b].size;
    }

    // A camera drag starts here, then remains one continuous touch until the
    // engine reaches a frame edge and immediately restarts from this point.
    e[i].type = SC_GAME_ELEMENT_AIM;
    snprintf(e[i].label, sizeof(e[i].label), "瞄准起点");
    e[i].aim.start.x = 0.550f;
    e[i].aim.start.y = 0.250f;
}
