#include "game_engine.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#include "android/input.h"
#include "control_msg.h"
#include "input_events.h"
#include "util/log.h"
#include <stdio.h>
#include <stdlib.h>
#include "util/tick.h"

// Engine tick rate
#define SC_GAME_TICK_INTERVAL SC_TICK_FROM_MS(16) // ~60Hz

// A tap injects DOWN, waits this many ticks, then UP (3 ticks ~ 50ms)
#define SC_GAME_TAP_TICKS 3

// When the aim pointer reaches this safe inset, the drag gesture is restarted
// immediately from its configured start point. Keeping a small inset avoids
// device-edge gestures while still providing a large continuous drag range.
#define SC_GAME_AIM_EDGE_RATIO 0.08f

// Bound pathological input deltas so one tick cannot flood the controller
// queue. Normal mouse input needs at most one restart per tick; even very
// large synthetic deltas still get many complete segments before the
// unrepresentable tail is discarded.
#define SC_GAME_AIM_MAX_RESETS_PER_TICK 64

// The joystick thumb glides from its current position to the deflected
// target within this time (ease-out). A teleporting touch is a bot signature.
#define SC_GAME_JOY_SLIDE_MS 180.0f

static bool
push_touch(struct sc_game_engine *eng, uint64_t pointer_id,
           enum android_motionevent_action action, int32_t x, int32_t y) {
    // Aim is maintained in frame pixels (unlike the other normalized touch
    // streams), and a large relative-motion delta or edge jitter may move it
    // just outside the frame before the boundary-rearm logic runs. Never send
    // invalid Android coordinates. Apart from preventing rejected/glitchy
    // events, clamping the final UP keeps it paired with the visible edge.
    if (!eng->frame_size_valid || eng->frame_size.width <= 0
            || eng->frame_size.height <= 0) {
        return false;
    }
    if (x < 0) {
        x = 0;
    } else if (x >= eng->frame_size.width) {
        x = eng->frame_size.width - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= eng->frame_size.height) {
        y = eng->frame_size.height - 1;
    }

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
    msg.inject_touch_event.action = action;
    msg.inject_touch_event.pointer_id = pointer_id;
    msg.inject_touch_event.position.screen_size = eng->frame_size;
    msg.inject_touch_event.position.point.x = x;
    msg.inject_touch_event.position.point.y = y;
    msg.inject_touch_event.pressure =
        action == AMOTION_EVENT_ACTION_UP ? 0.0f : 1.0f;
    msg.inject_touch_event.action_button = 0;
    msg.inject_touch_event.buttons = 0;

    if (!sc_controller_push_msg(eng->controller, &msg)) {
        LOGW("Game mode: could not inject touch event");
        return false;
    }
    return true;
}

static float
clampf(float value, float min, float max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static const struct sc_game_element *
get_aim_element(const struct sc_game_engine *eng) {
    if (!eng->profile.preset_count
            || eng->profile.current_preset >= eng->profile.preset_count) {
        return NULL;
    }

    const struct sc_game_preset *preset =
        &eng->profile.presets[eng->profile.current_preset];
    if (!preset->aim.enabled) {
        return NULL;
    }
    for (size_t i = 0; i < preset->element_count; ++i) {
        if (preset->elements[i].type == SC_GAME_ELEMENT_AIM) {
            return &preset->elements[i];
        }
    }
    return NULL;
}

// Start (or keep) the continuous aim stream. Must be called with the engine
// mutex held. The configured point is exact: unlike tap bindings, aim start
// must not wander due to humanization jitter.
static bool
start_aim_touch_locked(struct sc_game_engine *eng) {
    if (eng->aim_touch_active) {
        return true;
    }
    if (!eng->active || !eng->captured || !eng->frame_size_valid) {
        return false;
    }
    const struct sc_game_element *aim = get_aim_element(eng);
    if (!aim) {
        return false;
    }

    float max_x = eng->frame_size.width - 1;
    float max_y = eng->frame_size.height - 1;
    eng->aim_pos_x = clampf(aim->aim.start.x * eng->frame_size.width,
                            0.0f, max_x);
    eng->aim_pos_y = clampf(aim->aim.start.y * eng->frame_size.height,
                            0.0f, max_y);
    if (!push_touch(eng, SC_GAME_POINTER_ID_AIM,
                    AMOTION_EVENT_ACTION_DOWN,
                    (int32_t) eng->aim_pos_x,
                    (int32_t) eng->aim_pos_y)) {
        return false;
    }
    eng->aim_touch_active = true;
    return true;
}

static void
push_touch_norm(struct sc_game_engine *eng, uint64_t pointer_id,
                enum android_motionevent_action action, float nx, float ny) {
    int32_t x = (int32_t) (nx * eng->frame_size.width);
    int32_t y = (int32_t) (ny * eng->frame_size.height);
    if (x < 0) {
        x = 0;
    }
    if (x >= eng->frame_size.width) {
        x = eng->frame_size.width - 1;
    }
    if (y < 0) {
        y = 0;
    }
    if (y >= eng->frame_size.height) {
        y = eng->frame_size.height - 1;
    }
    push_touch(eng, pointer_id, action, x, y);
}

static uint8_t
mouse_button_mods(const struct sc_game_engine *eng, uint32_t button) {
    for (unsigned i = 0; i < 32; ++i) {
        if (button == (UINT32_C(1) << i)) {
            return eng->pressed_mouse_mods[i];
        }
    }
    return 0;
}

static void
set_mouse_button_mods(struct sc_game_engine *eng, uint32_t button,
                      uint8_t mods) {
    for (unsigned i = 0; i < 32; ++i) {
        if (button == (UINT32_C(1) << i)) {
            eng->pressed_mouse_mods[i] = mods;
            return;
        }
    }
}

// pressed_keys stores sc_keycode values (SDL keycode space), so a bound key
// matches by direct value comparison. Combo keys additionally require their
// modifiers to be currently held.
static bool
key_pressed(struct sc_game_engine *eng, const struct sc_game_key *key) {
    if (key->type == SC_GAME_KEY_MOUSE) {
        return (eng->pressed_mouse & key->value)
            && !(key->mods & ~mouse_button_mods(eng, key->value));
    }
    if (key->type != SC_GAME_KEY_KEYBOARD) {
        return false;
    }
    if (key->mods & ~eng->cur_mods) {
        return false;
    }
    for (size_t i = 0; i < eng->pressed_key_count; ++i) {
        if (eng->pressed_keys[i] == key->value) {
            return !(key->mods & ~eng->pressed_key_mods[i]);
        }
    }
    return false;
}

static uint32_t
rng_next(struct sc_game_engine *eng) {
    // xorshift32: a small, portable generator sufficient for input jitter.
    uint32_t x = eng->rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    eng->rng_state = x;
    return x;
}

// Random humanized jitter around 0, normalized relative to frame height.
static float
jitter(struct sc_game_engine *eng, const struct sc_game_humanize *h) {
    if (!h->enabled || !h->amp) {
        return 0.0f;
    }
    int32_t sample = (int32_t) (rng_next(eng) % 2001) - 1000;
    return (sample / 1000.0f) * h->amp;
}

// Mark binds matching this press so an entire physical press shorter than one
// engine tick is never lost. TAP keeps its timed pulse; HOLD emits a minimal
// paired DOWN/UP if the key has already gone up when the next tick samples it.
static void
mark_tap_pending(struct sc_game_engine *eng, uint32_t key_event_code,
                 uint8_t cur_mods, bool mouse) {
    const struct sc_game_preset *preset =
        &eng->profile.presets[eng->profile.current_preset];
    for (size_t i = 0; i < preset->element_count; ++i) {
        const struct sc_game_element *elem = &preset->elements[i];
        if (elem->type != SC_GAME_ELEMENT_BIND) {
            continue;
        }
        const struct sc_game_key *k = &elem->bind.key;
        if (k->type != (mouse ? SC_GAME_KEY_MOUSE : SC_GAME_KEY_KEYBOARD)) {
            continue;
        }
        if (k->value != key_event_code || (k->mods & ~cur_mods)) {
            continue;
        }
        eng->bind_press_pending[i] = true;
    }
}

// The single key of an element, if any (NULL for joysticks, which have four).
static const struct sc_game_key *
elem_key_of(const struct sc_game_element *elem) {
    switch (elem->type) {
        case SC_GAME_ELEMENT_BIND:
            return &elem->bind.key;
        case SC_GAME_ELEMENT_SWIPE:
            return &elem->swipe.key;
        default:
            return NULL;
    }
}

// Does the current preset reference this key (sc_keycode space), with its
// combo modifiers currently satisfiable?
static bool
uses_key(const struct sc_game_preset *preset, uint32_t key_event_code,
         uint8_t cur_mods) {
    for (size_t i = 0; i < preset->element_count; ++i) {
        const struct sc_game_element *elem = &preset->elements[i];
        if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
            const struct sc_game_key dirs[4] = {
                elem->joystick.up, elem->joystick.down,
                elem->joystick.left, elem->joystick.right,
            };
            for (size_t d = 0; d < 4; ++d) {
                if (dirs[d].type == SC_GAME_KEY_KEYBOARD
                        && dirs[d].value == key_event_code
                        && !(dirs[d].mods & ~cur_mods)) {
                    return true;
                }
            }
        } else {
            const struct sc_game_key *k = elem_key_of(elem);
            if (k && k->type == SC_GAME_KEY_KEYBOARD
                    && k->value == key_event_code && !(k->mods & ~cur_mods)) {
                return true;
            }
        }
    }
    return false;
}

static bool
uses_mouse_button(const struct sc_game_preset *preset, uint32_t button,
                  uint8_t cur_mods) {
    for (size_t i = 0; i < preset->element_count; ++i) {
        const struct sc_game_element *elem = &preset->elements[i];
        if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
            const struct sc_game_key dirs[4] = {
                elem->joystick.up, elem->joystick.down,
                elem->joystick.left, elem->joystick.right,
            };
            for (size_t d = 0; d < 4; ++d) {
                if (dirs[d].type == SC_GAME_KEY_MOUSE
                        && dirs[d].value == button
                        && !(dirs[d].mods & ~cur_mods)) {
                    return true;
                }
            }
        } else {
            const struct sc_game_key *k = elem_key_of(elem);
            if (k && k->type == SC_GAME_KEY_MOUSE && k->value == button
                    && !(k->mods & ~cur_mods)) {
                return true;
            }
        }
    }
    return false;
}

static void
release_all_locked(struct sc_game_engine *eng) {
    if (eng->frame_size_valid && eng->joy_touch_active) {
        push_touch_norm(eng, SC_GAME_POINTER_ID_JOYSTICK,
                        AMOTION_EVENT_ACTION_UP,
                        eng->joy_last_x, eng->joy_last_y);
    }
    if (eng->frame_size_valid && eng->aim_touch_active) {
        // aim_pos_* are device pixels: push_touch (no scaling)
        push_touch(eng, SC_GAME_POINTER_ID_AIM, AMOTION_EVENT_ACTION_UP,
                   (int32_t) eng->aim_pos_x, (int32_t) eng->aim_pos_y);
    }
    for (size_t i = 0; i < SC_GAME_MAX_ELEMENTS; ++i) {
        if (eng->frame_size_valid && eng->bind_touch_active[i]) {
            push_touch_norm(eng, SC_GAME_POINTER_ID_BIND_BASE - i,
                            AMOTION_EVENT_ACTION_UP,
                            eng->bind_last_x[i], eng->bind_last_y[i]);
        }
    }

    memset(eng->pressed_keys, 0, sizeof(eng->pressed_keys));
    memset(eng->pressed_key_mods, 0, sizeof(eng->pressed_key_mods));
    eng->pressed_key_count = 0;
    eng->pressed_mouse = 0;
    memset(eng->pressed_mouse_mods, 0, sizeof(eng->pressed_mouse_mods));
    eng->cur_mods = 0;

    eng->joy_touch_active = false;
    eng->joy_vec_x = 0;
    eng->joy_vec_y = 0;
    eng->joy_last_x = 0;
    eng->joy_last_y = 0;
    eng->joy_sliding = false;
    eng->joy_slide_from_x = 0;
    eng->joy_slide_from_y = 0;
    eng->joy_slide_progress = 0;
    eng->joy_tgt_x = 0;
    eng->joy_tgt_y = 0;

    eng->aim_touch_active = false;
    eng->aim_pos_x = 0;
    eng->aim_pos_y = 0;
    eng->aim_acc_x = 0;
    eng->aim_acc_y = 0;

    memset(eng->bind_pressed, 0, sizeof(eng->bind_pressed));
    memset(eng->bind_touch_active, 0, sizeof(eng->bind_touch_active));
    memset(eng->bind_press_pending, 0, sizeof(eng->bind_press_pending));
    memset(eng->bind_last_x, 0, sizeof(eng->bind_last_x));
    memset(eng->bind_last_y, 0, sizeof(eng->bind_last_y));
    memset(eng->bind_tap_pending, 0, sizeof(eng->bind_tap_pending));
    memset(eng->bind_tap_ticks, 0, sizeof(eng->bind_tap_ticks));
    memset(eng->swipe_gesture, 0, sizeof(eng->swipe_gesture));
    memset(eng->swipe_wait_ticks, 0, sizeof(eng->swipe_wait_ticks));
    memset(eng->swipe_progress, 0, sizeof(eng->swipe_progress));
    memset(eng->swipe_start_x, 0, sizeof(eng->swipe_start_x));
    memset(eng->swipe_start_y, 0, sizeof(eng->swipe_start_y));
    memset(eng->swipe_delta_x, 0, sizeof(eng->swipe_delta_x));
    memset(eng->swipe_delta_y, 0, sizeof(eng->swipe_delta_y));
}

static void
selftest_locked(struct sc_game_engine *eng, uint32_t dt_ms) {
    eng->selftest_ms += dt_ms;
    uint32_t t = eng->selftest_ms;
    uint8_t step = eng->selftest_step;

    // scripted: hold W 1s-2s, tap E 2.6s, ctrl+1 3.4s
    if (step == 0 && t >= 1000) {
        if (eng->pressed_key_count < SC_GAME_MAX_PRESSED_KEYS) {
            size_t i = eng->pressed_key_count++;
            eng->pressed_keys[i] = 119; // W
            eng->pressed_key_mods[i] = eng->cur_mods;
            mark_tap_pending(eng, 119, 0, false);
        }
        fprintf(stderr, "SELFTEST: W down\n");
        eng->selftest_step = 1;
    } else if (step == 1 && t >= 2000) {
        for (size_t i = 0; i < eng->pressed_key_count; ++i) {
            if (eng->pressed_keys[i] == 119) {
                eng->pressed_keys[i] =
                    eng->pressed_keys[--eng->pressed_key_count];
                eng->pressed_key_mods[i] =
                    eng->pressed_key_mods[eng->pressed_key_count];
                break;
            }
        }
        fprintf(stderr, "SELFTEST: W up\n");
        eng->selftest_step = 2;
    } else if (step == 2 && t >= 2600) {
        if (eng->pressed_key_count < SC_GAME_MAX_PRESSED_KEYS) {
            size_t i = eng->pressed_key_count++;
            eng->pressed_keys[i] = 101; // E
            eng->pressed_key_mods[i] = eng->cur_mods;
            mark_tap_pending(eng, 101, 0, false);
        }
        fprintf(stderr, "SELFTEST: E down\n");
        eng->selftest_step = 3;
    } else if (step == 3 && t >= 2680) {
        for (size_t i = 0; i < eng->pressed_key_count; ++i) {
            if (eng->pressed_keys[i] == 101) {
                eng->pressed_keys[i] =
                    eng->pressed_keys[--eng->pressed_key_count];
                eng->pressed_key_mods[i] =
                    eng->pressed_key_mods[eng->pressed_key_count];
                break;
            }
        }
        fprintf(stderr, "SELFTEST: E up\n");
        eng->selftest_step = 4;
    } else if (step == 4 && t >= 3400) {
        eng->cur_mods |= SC_GAME_MOD_CTRL;
        if (eng->pressed_key_count < SC_GAME_MAX_PRESSED_KEYS) {
            size_t i = eng->pressed_key_count++;
            eng->pressed_keys[i] = 49; // 1
            eng->pressed_key_mods[i] = eng->cur_mods;
            mark_tap_pending(eng, 49, SC_GAME_MOD_CTRL, false);
        }
        fprintf(stderr, "SELFTEST: ctrl+1 down\n");
        eng->selftest_step = 5;
    } else if (step == 5 && t >= 3480) {
        for (size_t i = 0; i < eng->pressed_key_count; ++i) {
            if (eng->pressed_keys[i] == 49) {
                eng->pressed_keys[i] =
                    eng->pressed_keys[--eng->pressed_key_count];
                eng->pressed_key_mods[i] =
                    eng->pressed_key_mods[eng->pressed_key_count];
                break;
            }
        }
        eng->cur_mods = 0;
        fprintf(stderr, "SELFTEST: ctrl+1 up\n");
        eng->selftest_step = 6;
    }
}

static void
tick_locked(struct sc_game_engine *eng) {
    if (!eng->active || !eng->frame_size_valid) {
        return;
    }
    if (eng->selftest) {
        selftest_locked(eng, 16);
    }
    const struct sc_game_preset *preset =
        &eng->profile.presets[eng->profile.current_preset];

    float fw = eng->frame_size.width;
    float fh = eng->frame_size.height;

    for (size_t i = 0; i < preset->element_count; ++i) {
        const struct sc_game_element *elem = &preset->elements[i];
        if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
            float vx = 0.0f;
            float vy = 0.0f;
            if (key_pressed(eng, &elem->joystick.right)) {
                vx += 1.0f;
            }
            if (key_pressed(eng, &elem->joystick.left)) {
                vx -= 1.0f;
            }
            if (key_pressed(eng, &elem->joystick.down)) {
                vy += 1.0f;
            }
            if (key_pressed(eng, &elem->joystick.up)) {
                vy -= 1.0f;
            }
            float len = sqrtf(vx * vx + vy * vy);
            if (len > 1.0f) {
                vx /= len;
                vy /= len;
            }

            if (vx != 0.0f || vy != 0.0f) {
                float jx = jitter(eng, &preset->humanize) * 0.5f;
                float jy = jitter(eng, &preset->humanize) * 0.5f;
                float tx = elem->joystick.center.x
                         + vx * elem->joystick.radius * fh / fw;
                float ty = elem->joystick.center.y
                         + vy * elem->joystick.radius;
                bool retarget = eng->joy_touch_active
                    && (fabsf(tx - eng->joy_tgt_x) > 0.001f
                        || fabsf(ty - eng->joy_tgt_y) > 0.001f);
                if (!eng->joy_touch_active) {
                    // thumb lands on the base, then glides outward
                    push_touch_norm(eng, SC_GAME_POINTER_ID_JOYSTICK,
                                    AMOTION_EVENT_ACTION_DOWN,
                                    elem->joystick.center.x + jx,
                                    elem->joystick.center.y + jy);
                    eng->joy_touch_active = true;
                    eng->joy_last_x = elem->joystick.center.x + jx;
                    eng->joy_last_y = elem->joystick.center.y + jy;
                    eng->joy_slide_from_x = eng->joy_last_x;
                    eng->joy_slide_from_y = eng->joy_last_y;
                    eng->joy_slide_progress = 0;
                    eng->joy_sliding = true;
                } else if (retarget) {
                    // direction change: re-aim from the current position
                    eng->joy_slide_from_x = eng->joy_last_x;
                    eng->joy_slide_from_y = eng->joy_last_y;
                    eng->joy_slide_progress = 0;
                    eng->joy_sliding = true;
                }
                eng->joy_tgt_x = tx;
                eng->joy_tgt_y = ty;
                if (eng->joy_sliding) {
                    eng->joy_slide_progress += 16.0f / SC_GAME_JOY_SLIDE_MS;
                    if (eng->joy_slide_progress >= 1.0f) {
                        eng->joy_slide_progress = 1.0f;
                        eng->joy_sliding = false;
                    }
                }
                // ease-out quad: fast at first, decelerating into the target
                float p = eng->joy_slide_progress;
                float e = 1.0f - (1.0f - p) * (1.0f - p);
                float nx = eng->joy_slide_from_x
                         + (tx - eng->joy_slide_from_x) * e + jx;
                float ny = eng->joy_slide_from_y
                         + (ty - eng->joy_slide_from_y) * e + jy;
                push_touch_norm(eng, SC_GAME_POINTER_ID_JOYSTICK,
                                AMOTION_EVENT_ACTION_MOVE, nx, ny);
                eng->joy_last_x = nx;
                eng->joy_last_y = ny;
            } else if (eng->joy_touch_active) {
                // joy_last_* are normalized: scale like every other push
                push_touch_norm(eng, SC_GAME_POINTER_ID_JOYSTICK,
                                AMOTION_EVENT_ACTION_UP,
                                eng->joy_last_x, eng->joy_last_y);
                eng->joy_touch_active = false;
                eng->joy_sliding = false;
            }
            eng->joy_vec_x = vx;
            eng->joy_vec_y = vy;
        } else if (elem->type == SC_GAME_ELEMENT_BIND) {
            bool pressed = key_pressed(eng, &elem->bind.key);
            uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - i;

            if (elem->bind.action == SC_GAME_ACTION_HOLD) {
                bool press_edge = eng->bind_press_pending[i];
                eng->bind_press_pending[i] = false;
                if ((pressed || press_edge) && !eng->bind_touch_active[i]) {
                    push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_DOWN,
                                    elem->bind.center.x, elem->bind.center.y);
                    eng->bind_touch_active[i] = true;
                    eng->bind_last_x[i] = elem->bind.center.x;
                    eng->bind_last_y[i] = elem->bind.center.y;
                } else if (!pressed && eng->bind_touch_active[i]) {
                    push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_UP,
                                    eng->bind_last_x[i], eng->bind_last_y[i]);
                    eng->bind_touch_active[i] = false;
                }
            } else { // TAP
                if (eng->bind_press_pending[i]) {
                    // sub-tick press edge recorded by on_key: fire the tap
                    eng->bind_press_pending[i] = false;
                    float px = elem->bind.center.x
                             + jitter(eng, &preset->humanize);
                    float py = elem->bind.center.y
                             + jitter(eng, &preset->humanize);
                    push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_DOWN,
                                    px, py);
                    eng->bind_touch_active[i] = true;
                    eng->bind_last_x[i] = px;
                    eng->bind_last_y[i] = py;
                    eng->bind_tap_pending[i] = true;
                    eng->bind_tap_ticks[i] =
                        SC_GAME_TAP_TICKS + (rng_next(eng) % 2);
                } else if (pressed && !eng->bind_pressed[i]) {
                    float px = elem->bind.center.x
                             + jitter(eng, &preset->humanize);
                    float py = elem->bind.center.y
                             + jitter(eng, &preset->humanize);
                    push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_DOWN,
                                    px, py);
                    eng->bind_touch_active[i] = true;
                    eng->bind_last_x[i] = px;
                    eng->bind_last_y[i] = py;
                    eng->bind_tap_pending[i] = true;
                    eng->bind_tap_ticks[i] =
                        SC_GAME_TAP_TICKS + (rng_next(eng) % 2);
                } else if (!pressed && eng->bind_touch_active[i]
                        && eng->bind_tap_pending[i]) {
                    // released before the tap delay elapsed: release now
                    push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_UP,
                                    eng->bind_last_x[i], eng->bind_last_y[i]);
                    eng->bind_touch_active[i] = false;
                    eng->bind_tap_pending[i] = false;
                } else if (eng->bind_tap_pending[i]) {
                    if (--eng->bind_tap_ticks[i] <= 0) {
                        push_touch_norm(eng, pointer_id,
                                        AMOTION_EVENT_ACTION_UP,
                                        eng->bind_last_x[i],
                                        eng->bind_last_y[i]);
                        eng->bind_touch_active[i] = false;
                        eng->bind_tap_pending[i] = false;
                    }
                }
            }
            eng->bind_pressed[i] = pressed;
        } else if (elem->type == SC_GAME_ELEMENT_SWIPE) {
            bool pressed = key_pressed(eng, &elem->swipe.key);
            uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - i;

            if (!pressed) {
                if (eng->bind_touch_active[i]) {
                    push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_UP,
                                    eng->bind_last_x[i], eng->bind_last_y[i]);
                    eng->bind_touch_active[i] = false;
                }
                eng->swipe_gesture[i] = false;
                eng->swipe_wait_ticks[i] = 0;
                eng->bind_pressed[i] = false;
                continue;
            }

            if (!eng->swipe_gesture[i] && !eng->bind_touch_active[i]) {
                if (eng->swipe_wait_ticks[i] > 0) {
                    --eng->swipe_wait_ticks[i];
                    eng->bind_pressed[i] = true;
                    continue;
                }
                // start a new gesture with jittered endpoints
                float sx = elem->swipe.from.x
                         + jitter(eng, &preset->humanize) * 2;
                float sy = elem->swipe.from.y
                         + jitter(eng, &preset->humanize) * 2;
                float tx = elem->swipe.to.x
                         + jitter(eng, &preset->humanize) * 2;
                float ty = elem->swipe.to.y
                         + jitter(eng, &preset->humanize) * 2;
                eng->swipe_start_x[i] = sx;
                eng->swipe_start_y[i] = sy;
                eng->swipe_delta_x[i] = tx - sx;
                eng->swipe_delta_y[i] = ty - sy;
                eng->swipe_progress[i] = 0;
                eng->swipe_gesture[i] = true;
                push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_DOWN,
                                sx, sy);
                eng->bind_touch_active[i] = true;
                eng->bind_last_x[i] = sx;
                eng->bind_last_y[i] = sy;
            }

            if (eng->swipe_gesture[i] && eng->bind_touch_active[i]) {
                eng->swipe_progress[i] +=
                    16.0f / (float) elem->swipe.dur_ms;
                if (eng->swipe_progress[i] >= 1.0f) {
                    float tx = eng->swipe_start_x[i]
                             + eng->swipe_delta_x[i];
                    float ty = eng->swipe_start_y[i]
                             + eng->swipe_delta_y[i];
                    push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_UP,
                                    tx, ty);
                    eng->bind_touch_active[i] = false;
                    eng->swipe_gesture[i] = false;
                    eng->bind_last_x[i] = tx;
                    eng->bind_last_y[i] = ty;
                    int extra = (int) elem->swipe.every_ms
                              - (int) elem->swipe.dur_ms;
                    eng->swipe_wait_ticks[i] =
                        extra > 0 ? (extra + 15) / 16 : 0;
                } else {
                    float px = eng->swipe_start_x[i]
                             + eng->swipe_delta_x[i]
                                   * eng->swipe_progress[i];
                    float py = eng->swipe_start_y[i]
                             + eng->swipe_delta_y[i]
                                   * eng->swipe_progress[i];
                    push_touch_norm(eng, pointer_id, AMOTION_EVENT_ACTION_MOVE,
                                    px, py);
                    eng->bind_last_x[i] = px;
                    eng->bind_last_y[i] = py;
                }
            }
            eng->bind_pressed[i] = true;
        }
    }

    // aim: one continuous touch driven by relative mouse motion. Capture
    // starts the touch at the configured point; idle ticks deliberately do
    // nothing, so moving again continues the same Android pointer stream.
    float dx = eng->aim_acc_x * preset->aim.sensitivity * eng->aim_scale_x;
    float dy = eng->aim_acc_y * preset->aim.sensitivity * eng->aim_scale_y;
    eng->aim_acc_x = 0;
    eng->aim_acc_y = 0;
    if (eng->captured && preset->aim.enabled
            && (dx != 0.0f || dy != 0.0f)
            && start_aim_touch_locked(eng)) {
        float min_x = SC_GAME_AIM_EDGE_RATIO * fw;
        float min_y = SC_GAME_AIM_EDGE_RATIO * fh;
        float max_x = (1.0f - SC_GAME_AIM_EDGE_RATIO) * fw;
        float max_y = (1.0f - SC_GAME_AIM_EDGE_RATIO) * fh;
        max_x = fminf(max_x, fw - 1.0f);
        max_y = fminf(max_y, fh - 1.0f);

        float remaining_x = dx;
        float remaining_y = dy;
        for (unsigned reset = 0;
                reset <= SC_GAME_AIM_MAX_RESETS_PER_TICK;
                ++reset) {
            float target_x = eng->aim_pos_x + remaining_x;
            float target_y = eng->aim_pos_y + remaining_y;
            if (target_x >= min_x && target_x <= max_x
                    && target_y >= min_y && target_y <= max_y) {
                eng->aim_pos_x = target_x;
                eng->aim_pos_y = target_y;
                push_touch(eng, SC_GAME_POINTER_ID_AIM,
                           AMOTION_EVENT_ACTION_MOVE,
                           (int32_t) eng->aim_pos_x,
                           (int32_t) eng->aim_pos_y);
                break;
            }

            // Find the first safe-rectangle edge crossed by this movement.
            // start_aim_touch_locked() normally starts inside it; clamping
            // also makes an explicitly edge-adjacent configured start safe.
            eng->aim_pos_x = clampf(eng->aim_pos_x, min_x, max_x);
            eng->aim_pos_y = clampf(eng->aim_pos_y, min_y, max_y);
            float fraction = 1.0f;
            if (remaining_x > 0.0f && target_x > max_x) {
                fraction = fminf(fraction,
                                 (max_x - eng->aim_pos_x) / remaining_x);
            } else if (remaining_x < 0.0f && target_x < min_x) {
                fraction = fminf(fraction,
                                 (min_x - eng->aim_pos_x) / remaining_x);
            }
            if (remaining_y > 0.0f && target_y > max_y) {
                fraction = fminf(fraction,
                                 (max_y - eng->aim_pos_y) / remaining_y);
            } else if (remaining_y < 0.0f && target_y < min_y) {
                fraction = fminf(fraction,
                                 (min_y - eng->aim_pos_y) / remaining_y);
            }
            fraction = clampf(fraction, 0.0f, 1.0f);
            eng->aim_pos_x = clampf(eng->aim_pos_x
                                      + remaining_x * fraction,
                                    min_x, max_x);
            eng->aim_pos_y = clampf(eng->aim_pos_y
                                      + remaining_y * fraction,
                                    min_y, max_y);
            push_touch(eng, SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_MOVE,
                       (int32_t) eng->aim_pos_x,
                       (int32_t) eng->aim_pos_y);
            push_touch(eng, SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_UP,
                       (int32_t) eng->aim_pos_x,
                       (int32_t) eng->aim_pos_y);
            eng->aim_touch_active = false;

            remaining_x *= 1.0f - fraction;
            remaining_y *= 1.0f - fraction;
            if (!start_aim_touch_locked(eng)) {
                break;
            }
            if (remaining_x == 0.0f && remaining_y == 0.0f) {
                break;
            }
        }
    }
}

static int
run_engine(void *data) {
    struct sc_game_engine *eng = data;

    sc_mutex_lock(&eng->mutex);
    while (!eng->stopped) {
        // wait until the next tick
        sc_tick now = sc_tick_now();
        if (eng->deadline <= now) {
            eng->deadline = now + SC_GAME_TICK_INTERVAL;
            tick_locked(eng);
        }
        sc_cond_timedwait(&eng->cond, &eng->mutex, eng->deadline);
    }
    release_all_locked(eng);
    sc_mutex_unlock(&eng->mutex);
    return 0;
}

bool
sc_game_engine_init(struct sc_game_engine *eng,
                    struct sc_controller *controller) {
    assert(controller);

    eng->controller = controller;
    eng->active = false;
    eng->captured = false;
    eng->frame_size_valid = false;
    eng->frame_size.width = 0;
    eng->frame_size.height = 0;
    eng->aim_scale_x = 1.0f;
    eng->aim_scale_y = 1.0f;
    eng->pressed_mouse = 0;
    memset(eng->pressed_mouse_mods, 0, sizeof(eng->pressed_mouse_mods));
    memset(eng->pressed_keys, 0, sizeof(eng->pressed_keys));
    memset(eng->pressed_key_mods, 0, sizeof(eng->pressed_key_mods));
    eng->pressed_key_count = 0;
    eng->cur_mods = 0;
    eng->selftest = getenv("SCRPLAY_GAME_SELFTEST") != NULL;
    eng->selftest_ms = 0;
    {
        const char *d = getenv("SCRPLAY_GAME_SELFTEST_DELAY");
        long secs = d ? strtol(d, NULL, 10) : 0;
        if (secs > 0 && secs < 3600) {
            eng->selftest_ms = 0 - (uint32_t) secs * 1000;
        }
    }
    eng->selftest_step = 0;
    if (eng->selftest) {
        fprintf(stderr, "SELFTEST: scripted input mode armed\n");
    }
    eng->aim_acc_x = 0;
    eng->aim_acc_y = 0;
    eng->joy_touch_active = false;
    eng->joy_vec_x = 0;
    eng->joy_vec_y = 0;
    eng->joy_last_x = 0;
    eng->joy_last_y = 0;
    eng->joy_sliding = false;
    eng->joy_slide_from_x = 0;
    eng->joy_slide_from_y = 0;
    eng->joy_slide_progress = 0;
    eng->joy_tgt_x = 0;
    eng->joy_tgt_y = 0;
    eng->aim_touch_active = false;
    eng->aim_pos_x = 0;
    eng->aim_pos_y = 0;
    memset(eng->bind_pressed, 0, sizeof(eng->bind_pressed));
    memset(eng->bind_touch_active, 0, sizeof(eng->bind_touch_active));
    memset(eng->bind_press_pending, 0, sizeof(eng->bind_press_pending));
    memset(eng->bind_last_x, 0, sizeof(eng->bind_last_x));
    memset(eng->bind_last_y, 0, sizeof(eng->bind_last_y));
    memset(eng->bind_tap_pending, 0, sizeof(eng->bind_tap_pending));
    memset(eng->bind_tap_ticks, 0, sizeof(eng->bind_tap_ticks));
    memset(eng->swipe_gesture, 0, sizeof(eng->swipe_gesture));
    memset(eng->swipe_wait_ticks, 0, sizeof(eng->swipe_wait_ticks));
    memset(eng->swipe_progress, 0, sizeof(eng->swipe_progress));
    memset(eng->swipe_start_x, 0, sizeof(eng->swipe_start_x));
    memset(eng->swipe_start_y, 0, sizeof(eng->swipe_start_y));
    memset(eng->swipe_delta_x, 0, sizeof(eng->swipe_delta_x));
    memset(eng->swipe_delta_y, 0, sizeof(eng->swipe_delta_y));
    eng->thread_started = false;
    eng->rng_state = (uint32_t) sc_tick_now();
    if (!eng->rng_state) {
        // xorshift32 has an all-zero absorbing state.
        eng->rng_state = UINT32_C(0x9e3779b9);
    }
    eng->stopped = false;
    eng->deadline = 0;

    if (!sc_mutex_init(&eng->mutex)) {
        return false;
    }
    if (!sc_cond_init(&eng->cond)) {
        sc_mutex_destroy(&eng->mutex);
        return false;
    }
    return true;
}

bool
sc_game_engine_start(struct sc_game_engine *eng) {
    sc_mutex_lock(&eng->mutex);
    eng->deadline = sc_tick_now() + SC_GAME_TICK_INTERVAL;
    sc_mutex_unlock(&eng->mutex);

    bool ok = sc_thread_create(&eng->thread, run_engine, "scrplay-game", eng);
    eng->thread_started = ok;
    if (!ok) {
        LOGE("Game mode: could not start injection thread");
    }
    return ok;
}

void
sc_game_engine_stop(struct sc_game_engine *eng) {
    if (!eng->thread_started) {
        return;
    }
    sc_mutex_lock(&eng->mutex);
    eng->stopped = true;
    sc_cond_signal(&eng->cond);
    sc_mutex_unlock(&eng->mutex);

    sc_thread_join(&eng->thread, NULL);
    eng->thread_started = false;
}

void
sc_game_engine_destroy(struct sc_game_engine *eng) {
    sc_game_engine_stop(eng);
    sc_cond_destroy(&eng->cond);
    sc_mutex_destroy(&eng->mutex);
}

void
sc_game_engine_set_active(struct sc_game_engine *eng, bool active) {
    sc_mutex_lock(&eng->mutex);
    if (eng->active && !active) {
        release_all_locked(eng);
    }
    eng->active = active;
    if (active) {
        start_aim_touch_locked(eng);
    }
    sc_mutex_unlock(&eng->mutex);
}

void
sc_game_engine_set_captured(struct sc_game_engine *eng, bool captured) {
    sc_mutex_lock(&eng->mutex);
    if (!captured) {
        release_all_locked(eng);
    }
    eng->captured = captured;
    if (captured) {
        start_aim_touch_locked(eng);
    }
    sc_mutex_unlock(&eng->mutex);
}

void
sc_game_engine_set_frame_size(struct sc_game_engine *eng,
                              struct sc_size frame_size, float aim_scale_x,
                              float aim_scale_y) {
    sc_mutex_lock(&eng->mutex);
    bool changed = !eng->frame_size_valid
                || eng->frame_size.width != frame_size.width
                || eng->frame_size.height != frame_size.height;
    if (changed && eng->frame_size_valid && eng->active) {
        // rotation or resolution change: release touches before switching
        release_all_locked(eng);
    }
    eng->frame_size = frame_size;
    eng->frame_size_valid = frame_size.width > 0 && frame_size.height > 0;
    eng->aim_scale_x = aim_scale_x;
    eng->aim_scale_y = aim_scale_y;
    start_aim_touch_locked(eng);
    sc_mutex_unlock(&eng->mutex);
}

void
sc_game_engine_set_profile(struct sc_game_engine *eng,
                           const struct sc_game_profile *profile) {
    sc_mutex_lock(&eng->mutex);
    // The runtime arrays and last touch positions describe the old snapshot.
    // Release them before overwriting it, regardless of which field changed.
    // This also prevents held physical keys from silently activating bindings
    // in a freshly loaded profile.
    release_all_locked(eng);
    eng->profile = *profile;
    if (eng->profile.current_preset >= eng->profile.preset_count) {
        eng->profile.current_preset = 0;
    }
    start_aim_touch_locked(eng);
    sc_mutex_unlock(&eng->mutex);
}

void
sc_game_engine_next_preset(struct sc_game_engine *eng) {
    sc_mutex_lock(&eng->mutex);
    if (eng->profile.preset_count) {
        release_all_locked(eng);
        eng->profile.current_preset =
            (eng->profile.current_preset + 1) % eng->profile.preset_count;
        LOGI("Game mode: preset '%s'",
             eng->profile.presets[eng->profile.current_preset].name);
        start_aim_touch_locked(eng);
    }
    sc_mutex_unlock(&eng->mutex);
}

bool
sc_game_engine_on_key(struct sc_game_engine *eng, uint32_t key_event_code,
                      bool down) {
    sc_mutex_lock(&eng->mutex);
    size_t tracked = SIZE_MAX;
    for (size_t i = 0; i < eng->pressed_key_count; ++i) {
        if (eng->pressed_keys[i] == key_event_code) {
            tracked = i;
            break;
        }
    }

    bool used;
    if (!down) {
        // Ownership is established exclusively by DOWN. Re-evaluating an
        // untracked UP against the *current* modifiers would swallow the
        // release of a key whose DOWN was forwarded to Android (for example:
        // 1 down, Ctrl down, 1 up for a Ctrl+1 mapping), leaving it stuck.
        used = tracked != SIZE_MAX;
        if (used) {
            size_t last = --eng->pressed_key_count;
            eng->pressed_keys[tracked] = eng->pressed_keys[last];
            eng->pressed_key_mods[tracked] = eng->pressed_key_mods[last];
            eng->pressed_keys[last] = 0;
            eng->pressed_key_mods[last] = 0;
        }
    } else {
        const struct sc_game_preset *preset =
            &eng->profile.presets[eng->profile.current_preset];
        used = eng->active
            && uses_key(preset, key_event_code, eng->cur_mods);
        if (used && down && tracked == SIZE_MAX) {
            // SDL repeat DOWNs are ignored. A single physical press owns one
            // tracked slot, so one UP is sufficient to release it.
            if (eng->pressed_key_count < SC_GAME_MAX_PRESSED_KEYS) {
                size_t i = eng->pressed_key_count++;
                eng->pressed_keys[i] = key_event_code;
                eng->pressed_key_mods[i] = eng->cur_mods;
                mark_tap_pending(eng, key_event_code, eng->cur_mods, false);
            }
        }
    }
    sc_mutex_unlock(&eng->mutex);
    return used;
}

bool
sc_game_engine_on_mouse_button(struct sc_game_engine *eng,
                               uint32_t mouse_button, bool down) {
    sc_mutex_lock(&eng->mutex);
    bool tracked = eng->pressed_mouse & mouse_button;
    bool used;
    if (!down) {
        // As for keyboard keys, never claim an UP whose DOWN was not claimed.
        used = tracked;
        if (tracked) {
            eng->pressed_mouse &= ~mouse_button;
            set_mouse_button_mods(eng, mouse_button, 0);
        }
    } else {
        const struct sc_game_preset *preset =
            &eng->profile.presets[eng->profile.current_preset];
        used = eng->active
            && uses_mouse_button(preset, mouse_button, eng->cur_mods);
        if (used && down && !tracked) {
            eng->pressed_mouse |= mouse_button;
            set_mouse_button_mods(eng, mouse_button, eng->cur_mods);
            mark_tap_pending(eng, mouse_button, eng->cur_mods, true);
        }
    }
    sc_mutex_unlock(&eng->mutex);
    return used;
}

void
sc_game_engine_set_mods(struct sc_game_engine *eng, uint8_t mods) {
    sc_mutex_lock(&eng->mutex);
    uint8_t released = eng->cur_mods & ~mods;
    if (released) {
        // Remember only modifiers held continuously since the physical DOWN.
        // Clearing (rather than recomputing from current state) prevents a
        // released/re-pressed modifier from resurrecting an old base key.
        // Unrelated modifier releases leave the mapping eligible.
        for (size_t i = 0; i < eng->pressed_key_count; ++i) {
            eng->pressed_key_mods[i] &= ~released;
        }
        for (unsigned i = 0; i < 32; ++i) {
            uint32_t button = UINT32_C(1) << i;
            if (eng->pressed_mouse & button) {
                eng->pressed_mouse_mods[i] &= ~released;
            }
        }
    }
    eng->cur_mods = mods;
    sc_mutex_unlock(&eng->mutex);
}

void
sc_game_engine_add_aim_delta(struct sc_game_engine *eng, float dx, float dy) {
    sc_mutex_lock(&eng->mutex);
    eng->aim_acc_x += dx;
    eng->aim_acc_y += dy;
    sc_mutex_unlock(&eng->mutex);
}

void
sc_game_engine_release_all(struct sc_game_engine *eng) {
    sc_mutex_lock(&eng->mutex);
    release_all_locked(eng);
    sc_mutex_unlock(&eng->mutex);
}

#ifdef SC_TEST
void
sc_game_engine_tick_for_test(struct sc_game_engine *eng) {
    sc_mutex_lock(&eng->mutex);
    tick_locked(eng);
    sc_mutex_unlock(&eng->mutex);
}
#endif
