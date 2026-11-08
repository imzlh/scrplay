#include "common.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "android/input.h"
#include "control_msg.h"
#include "game/game_engine.h"
#include "input_events.h"

#define MAX_RECORDED_MSGS 512

static struct sc_control_msg recorded_msgs[MAX_RECORDED_MSGS];
static size_t recorded_msg_count;

// The engine unit test deliberately does not link controller.c. Capturing the
// messages here makes every tick deterministic and lets the test verify touch
// stream pairing without a socket or an engine thread.
bool
sc_controller_push_msg(struct sc_controller *controller,
                       const struct sc_control_msg *msg) {
    (void) controller;
    assert(recorded_msg_count < ARRAY_LEN(recorded_msgs));
    recorded_msgs[recorded_msg_count++] = *msg;
    return true;
}

static void
reset_recording(void) {
    memset(recorded_msgs, 0, sizeof(recorded_msgs));
    recorded_msg_count = 0;
}

static size_t
count_touch(uint64_t pointer_id, enum android_motionevent_action action) {
    size_t count = 0;
    for (size_t i = 0; i < recorded_msg_count; ++i) {
        const struct sc_control_msg *msg = &recorded_msgs[i];
        if (msg->type == SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT
                && msg->inject_touch_event.pointer_id == pointer_id
                && msg->inject_touch_event.action == action) {
            ++count;
        }
    }
    return count;
}

static const struct sc_control_msg *
find_last_touch(uint64_t pointer_id, enum android_motionevent_action action) {
    for (size_t i = recorded_msg_count; i; --i) {
        const struct sc_control_msg *msg = &recorded_msgs[i - 1];
        if (msg->type == SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT
                && msg->inject_touch_event.pointer_id == pointer_id
                && msg->inject_touch_event.action == action) {
            return msg;
        }
    }
    return NULL;
}

static struct sc_game_key
keyboard_key(uint32_t value, uint8_t mods) {
    return (struct sc_game_key) {
        .type = SC_GAME_KEY_KEYBOARD,
        .value = value,
        .mods = mods,
    };
}

static struct sc_game_key
mouse_key(uint32_t value, uint8_t mods) {
    return (struct sc_game_key) {
        .type = SC_GAME_KEY_MOUSE,
        .value = value,
        .mods = mods,
    };
}

static void
init_profile(struct sc_game_profile *profile, const char *preset_name) {
    memset(profile, 0, sizeof(*profile));
    memcpy(profile->name, "engine_test", sizeof("engine_test"));
    profile->preset_count = 1;
    memcpy(profile->presets[0].name, preset_name, strlen(preset_name) + 1);
    profile->presets[0].aim.sensitivity = 1.0f;
}

static size_t
add_hold(struct sc_game_profile *profile, struct sc_game_key key,
         float x, float y) {
    struct sc_game_preset *preset = &profile->presets[0];
    assert(preset->element_count < SC_GAME_MAX_ELEMENTS);
    size_t index = preset->element_count++;
    preset->elements[index] = (struct sc_game_element) {
        .type = SC_GAME_ELEMENT_BIND,
        .bind = {
            .key = key,
            .action = SC_GAME_ACTION_HOLD,
            .center = {x, y},
            .size = 0.05f,
        },
    };
    return index;
}

static size_t
add_tap(struct sc_game_profile *profile, struct sc_game_key key,
        float x, float y) {
    struct sc_game_preset *preset = &profile->presets[0];
    assert(preset->element_count < SC_GAME_MAX_ELEMENTS);
    size_t index = preset->element_count++;
    preset->elements[index] = (struct sc_game_element) {
        .type = SC_GAME_ELEMENT_BIND,
        .bind = {
            .key = key,
            .action = SC_GAME_ACTION_TAP,
            .center = {x, y},
            .size = 0.05f,
        },
    };
    return index;
}

static size_t
add_joystick(struct sc_game_profile *profile, float x, float y,
             float radius) {
    struct sc_game_preset *preset = &profile->presets[0];
    assert(preset->element_count < SC_GAME_MAX_ELEMENTS);
    size_t index = preset->element_count++;
    preset->elements[index] = (struct sc_game_element) {
        .type = SC_GAME_ELEMENT_JOYSTICK,
        .joystick = {
            .center = {x, y},
            .radius = radius,
            .up = {
                .type = SC_GAME_KEY_KEYBOARD,
                .value = 'w',
            },
            .down = {
                .type = SC_GAME_KEY_KEYBOARD,
                .value = 's',
            },
            .left = {
                .type = SC_GAME_KEY_KEYBOARD,
                .value = 'a',
            },
            .right = {
                .type = SC_GAME_KEY_KEYBOARD,
                .value = 'd',
            },
        },
    };
    return index;
}

static size_t
add_swipe(struct sc_game_profile *profile, struct sc_game_key key) {
    struct sc_game_preset *preset = &profile->presets[0];
    assert(preset->element_count < SC_GAME_MAX_ELEMENTS);
    size_t index = preset->element_count++;
    preset->elements[index] = (struct sc_game_element) {
        .type = SC_GAME_ELEMENT_SWIPE,
        .swipe = {
            .key = key,
            .from = {0.30f, 0.70f},
            .to = {0.45f, 0.45f},
            .dur_ms = 1000,
            .every_ms = 1200,
        },
    };
    return index;
}

static size_t
add_aim_start(struct sc_game_profile *profile) {
    struct sc_game_preset *preset = &profile->presets[0];
    assert(preset->element_count < SC_GAME_MAX_ELEMENTS);
    size_t index = preset->element_count++;
    preset->elements[index] = (struct sc_game_element) {
        .type = SC_GAME_ELEMENT_AIM,
        .aim = {
            .start = {0.60f, 0.35f},
        },
    };
    return index;
}

struct fixture {
    struct sc_controller controller;
    struct sc_game_engine engine;
};

static void
fixture_init(struct fixture *fixture,
             const struct sc_game_profile *profile) {
    memset(fixture, 0, sizeof(*fixture));
    assert(sc_game_engine_init(&fixture->engine, &fixture->controller));
    // Never let a developer's smoke-test environment variable make this unit
    // test inject its scripted timeline.
    fixture->engine.selftest = false;
    sc_game_engine_set_profile(&fixture->engine, profile);
    sc_game_engine_set_frame_size(&fixture->engine,
                                  (struct sc_size) {1920, 1080}, 1.0f, 1.0f);
    sc_game_engine_set_active(&fixture->engine, true);
    reset_recording();
}

static void
fixture_destroy(struct fixture *fixture) {
    sc_game_engine_release_all(&fixture->engine);
    sc_game_engine_destroy(&fixture->engine);
}

static void
tick(struct fixture *fixture) {
    sc_game_engine_tick_for_test(&fixture->engine);
}

static void
test_key_up_after_modifier_change(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "combo_up");
    size_t bind = add_hold(&profile,
                           keyboard_key('1', SC_GAME_MOD_CTRL), 0.75f, 0.70f);
    uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - bind;

    struct fixture fixture;
    fixture_init(&fixture, &profile);

    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    assert(sc_game_engine_on_key(&fixture.engine, '1', true));
    assert(fixture.engine.pressed_key_count == 1);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    // Releasing Ctrl invalidates this physical press. Pressing Ctrl again
    // while '1' is still physically held must not resurrect the touch.
    sc_game_engine_set_mods(&fixture.engine, 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_UP) == 1);

    // Ownership comes from the tracked DOWN, not from the modifiers visible
    // at UP time. Otherwise the event leaks to Android and the local key
    // remains stuck in the engine.
    assert(sc_game_engine_on_key(&fixture.engine, '1', false));
    assert(fixture.engine.pressed_key_count == 0);
    tick(&fixture);

    fixture_destroy(&fixture);
}

static void
test_ctrl_combo_modifier_first_and_no_resurrection(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "combo_resurrection");
    size_t bind = add_hold(&profile,
                           keyboard_key('1', SC_GAME_MOD_CTRL), 0.75f, 0.70f);
    uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - bind;

    struct fixture fixture;
    fixture_init(&fixture, &profile);

    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    assert(sc_game_engine_on_key(&fixture.engine, '1', true));
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    sc_game_engine_set_mods(&fixture.engine, 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_UP) == 1);

    // The base key is still physically held, but its combo activation was
    // cancelled when Ctrl went up. A later Ctrl press must not revive it.
    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    assert(sc_game_engine_on_key(&fixture.engine, '1', false));
    assert(fixture.engine.pressed_key_count == 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    fixture_destroy(&fixture);
}

static void
test_unrelated_modifier_release_keeps_combo_active(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "combo_unrelated_modifier");
    size_t bind = add_hold(&profile,
                           keyboard_key('1', SC_GAME_MOD_CTRL), 0.75f, 0.70f);
    uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - bind;

    struct fixture fixture;
    fixture_init(&fixture, &profile);
    sc_game_engine_set_mods(&fixture.engine,
                            SC_GAME_MOD_CTRL | SC_GAME_MOD_SHIFT);
    assert(sc_game_engine_on_key(&fixture.engine, '1', true));
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    // Shift was present at DOWN but is not required by Ctrl+1.
    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_UP) == 0);

    sc_game_engine_set_mods(&fixture.engine, 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_UP) == 1);
    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);
    assert(sc_game_engine_on_key(&fixture.engine, '1', false));

    fixture_destroy(&fixture);
}

static void
test_ctrl_combo_base_key_first(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "combo_base_first");
    size_t bind = add_hold(&profile,
                           keyboard_key('1', SC_GAME_MOD_CTRL), 0.75f, 0.70f);
    uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - bind;

    struct fixture fixture;
    fixture_init(&fixture, &profile);
    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    assert(sc_game_engine_on_key(&fixture.engine, '1', true));
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    assert(sc_game_engine_on_key(&fixture.engine, '1', false));
    assert(fixture.engine.pressed_key_count == 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_UP) == 1);
    sc_game_engine_set_mods(&fixture.engine, 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    fixture_destroy(&fixture);
}

static void
test_mouse_combo_lifecycle(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "mouse_combo");
    size_t bind = add_hold(&profile,
                           mouse_key(SC_MOUSE_BUTTON_LEFT, SC_GAME_MOD_CTRL),
                           0.82f, 0.72f);
    uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - bind;

    struct fixture fixture;
    fixture_init(&fixture, &profile);
    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    assert(sc_game_engine_on_mouse_button(&fixture.engine,
                                          SC_MOUSE_BUTTON_LEFT, true));
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    sc_game_engine_set_mods(&fixture.engine, 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_UP) == 1);

    assert(sc_game_engine_on_mouse_button(&fixture.engine,
                                          SC_MOUSE_BUTTON_LEFT, false));
    assert(!(fixture.engine.pressed_mouse & SC_MOUSE_BUTTON_LEFT));

    // Start a second physical press to exercise modifier resurrection.
    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    assert(sc_game_engine_on_mouse_button(&fixture.engine,
                                          SC_MOUSE_BUTTON_LEFT, true));
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 2);

    sc_game_engine_set_mods(&fixture.engine, 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_UP) == 2);
    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 2);

    assert(sc_game_engine_on_mouse_button(&fixture.engine,
                                          SC_MOUSE_BUTTON_LEFT, false));
    assert(!(fixture.engine.pressed_mouse & SC_MOUSE_BUTTON_LEFT));
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 2);

    fixture_destroy(&fixture);
}

static void
test_repeat_down_does_not_stick(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "repeat");
    size_t bind = add_hold(&profile, keyboard_key('e', 0), 0.80f, 0.65f);
    uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - bind;

    struct fixture fixture;
    fixture_init(&fixture, &profile);
    assert(sc_game_engine_on_key(&fixture.engine, 'e', true));
    assert(sc_game_engine_on_key(&fixture.engine, 'e', true));
    assert(fixture.engine.pressed_key_count == 1);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN) == 1);

    assert(sc_game_engine_on_key(&fixture.engine, 'e', false));
    assert(fixture.engine.pressed_key_count == 0);
    tick(&fixture);
    assert(count_touch(pointer_id, AMOTION_EVENT_ACTION_UP) == 1);

    fixture_destroy(&fixture);
}

static void
test_untracked_up_is_not_claimed_after_modifier_change(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "up_ownership");
    add_hold(&profile, keyboard_key('1', SC_GAME_MOD_CTRL), 0.75f, 0.70f);
    add_hold(&profile,
             mouse_key(SC_MOUSE_BUTTON_LEFT, SC_GAME_MOD_CTRL),
             0.82f, 0.72f);

    struct fixture fixture;
    fixture_init(&fixture, &profile);

    // These DOWNs do not match and therefore belong to the real input
    // processor. Acquiring Ctrl in between must not let the engine steal the
    // corresponding UPs and leave Android with stuck input state.
    assert(!sc_game_engine_on_key(&fixture.engine, '1', true));
    assert(!sc_game_engine_on_mouse_button(&fixture.engine,
                                           SC_MOUSE_BUTTON_LEFT, true));
    sc_game_engine_set_mods(&fixture.engine, SC_GAME_MOD_CTRL);
    assert(!sc_game_engine_on_key(&fixture.engine, '1', false));
    assert(!sc_game_engine_on_mouse_button(&fixture.engine,
                                           SC_MOUSE_BUTTON_LEFT, false));
    assert(fixture.engine.pressed_key_count == 0);
    assert(fixture.engine.pressed_mouse == 0);
    tick(&fixture);
    assert(recorded_msg_count == 0);

    fixture_destroy(&fixture);
}

static void
test_sub_tick_hold_emits_paired_touch(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "short_hold");
    size_t bind = add_hold(&profile, keyboard_key('e', 0), 0.80f, 0.65f);
    uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - bind;

    struct fixture fixture;
    fixture_init(&fixture, &profile);

    // Both physical edges arrive before the 60 Hz engine samples state.
    assert(sc_game_engine_on_key(&fixture.engine, 'e', true));
    assert(sc_game_engine_on_key(&fixture.engine, 'e', false));
    tick(&fixture);

    // Preserve a minimum one-tick contact instead of sending DOWN+UP with the
    // same sampling timestamp (some games ignore a zero-duration touch).
    assert(recorded_msg_count == 1);
    assert(recorded_msgs[0].inject_touch_event.pointer_id == pointer_id);
    assert(recorded_msgs[0].inject_touch_event.action
           == AMOTION_EVENT_ACTION_DOWN);
    assert(fixture.engine.bind_touch_active[bind]);

    tick(&fixture);
    assert(recorded_msg_count == 2);
    assert(recorded_msgs[1].inject_touch_event.pointer_id == pointer_id);
    assert(recorded_msgs[1].inject_touch_event.action
           == AMOTION_EVENT_ACTION_UP);
    assert(!fixture.engine.bind_touch_active[bind]);

    fixture_destroy(&fixture);
}

static void
assert_runtime_state_cleared(const struct sc_game_engine *engine) {
    assert(engine->pressed_key_count == 0);
    assert(engine->pressed_mouse == 0);
    assert(engine->cur_mods == 0);
    assert(!engine->joy_touch_active);
    assert(!engine->joy_sliding);
    assert(engine->joy_vec_x == 0.0f);
    assert(engine->joy_vec_y == 0.0f);
    assert(!engine->aim_touch_active);
    assert(engine->aim_acc_x == 0.0f);
    assert(engine->aim_acc_y == 0.0f);

    for (size_t i = 0; i < SC_GAME_MAX_ELEMENTS; ++i) {
        assert(!engine->bind_pressed[i]);
        assert(!engine->bind_touch_active[i]);
        assert(!engine->bind_press_pending[i]);
        assert(!engine->bind_tap_pending[i]);
        assert(engine->bind_tap_ticks[i] == 0);
        assert(!engine->swipe_gesture[i]);
        assert(engine->swipe_wait_ticks[i] == 0);
        assert(engine->swipe_progress[i] == 0.0f);
    }
}

static void
test_release_all_pairs_every_stream_and_allows_fresh_input(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "release_all");
    size_t joy = add_joystick(&profile, 0.17f, 0.78f, 0.10f);
    size_t hold = add_hold(&profile, keyboard_key('e', 0), 0.82f, 0.68f);
    size_t swipe = add_swipe(&profile, keyboard_key('q', 0));
    size_t tap = add_tap(&profile, keyboard_key('1', 0), 0.75f, 0.82f);
    size_t mouse = add_hold(&profile,
                            mouse_key(SC_MOUSE_BUTTON_LEFT, 0),
                            0.90f, 0.75f);
    add_aim_start(&profile);
    profile.presets[0].aim.enabled = true;

    const uint64_t pointers[] = {
        SC_GAME_POINTER_ID_JOYSTICK,
        SC_GAME_POINTER_ID_BIND_BASE - hold,
        SC_GAME_POINTER_ID_BIND_BASE - swipe,
        SC_GAME_POINTER_ID_BIND_BASE - tap,
        SC_GAME_POINTER_ID_BIND_BASE - mouse,
        SC_GAME_POINTER_ID_AIM,
    };
    (void) joy;

    struct fixture fixture;
    fixture_init(&fixture, &profile);
    sc_game_engine_set_captured(&fixture.engine, true);
    assert(sc_game_engine_on_key(&fixture.engine, 'w', true));
    assert(sc_game_engine_on_key(&fixture.engine, 'e', true));
    assert(sc_game_engine_on_key(&fixture.engine, 'q', true));
    assert(sc_game_engine_on_key(&fixture.engine, '1', true));
    assert(sc_game_engine_on_mouse_button(&fixture.engine,
                                          SC_MOUSE_BUTTON_LEFT, true));
    sc_game_engine_add_aim_delta(&fixture.engine, 8.0f, -5.0f);
    tick(&fixture);

    for (size_t i = 0; i < ARRAY_LEN(pointers); ++i) {
        assert(count_touch(pointers[i], AMOTION_EVENT_ACTION_DOWN) == 1);
        assert(count_touch(pointers[i], AMOTION_EVENT_ACTION_UP) == 0);
    }

    sc_game_engine_release_all(&fixture.engine);
    for (size_t i = 0; i < ARRAY_LEN(pointers); ++i) {
        assert(count_touch(pointers[i], AMOTION_EVENT_ACTION_UP) == 1);
    }
    assert_runtime_state_cleared(&fixture.engine);

    // Focus recovery must not poison the next real press.
    reset_recording();
    assert(sc_game_engine_on_key(&fixture.engine, 'w', true));
    tick(&fixture);
    assert(count_touch(SC_GAME_POINTER_ID_JOYSTICK,
                       AMOTION_EVENT_ACTION_DOWN) == 1);

    fixture_destroy(&fixture);
}

static void
test_set_profile_releases_old_touch_before_replacement(void) {
    struct sc_game_profile old_profile;
    init_profile(&old_profile, "old");
    size_t old_bind = add_hold(&old_profile, keyboard_key('e', 0),
                               0.20f, 0.30f);
    uint64_t pointer_id = SC_GAME_POINTER_ID_BIND_BASE - old_bind;

    struct fixture fixture;
    fixture_init(&fixture, &old_profile);
    assert(sc_game_engine_on_key(&fixture.engine, 'e', true));
    tick(&fixture);
    const struct sc_control_msg *down =
        find_last_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN);
    assert(down);
    struct sc_point old_point = down->inject_touch_event.position.point;

    struct sc_game_profile new_profile;
    init_profile(&new_profile, "new");
    add_hold(&new_profile, keyboard_key('f', 0), 0.80f, 0.75f);
    sc_game_engine_set_profile(&fixture.engine, &new_profile);

    const struct sc_control_msg *up =
        find_last_touch(pointer_id, AMOTION_EVENT_ACTION_UP);
    assert(up);
    assert(up->inject_touch_event.position.point.x == old_point.x);
    assert(up->inject_touch_event.position.point.y == old_point.y);
    assert(!strcmp(fixture.engine.profile.presets[0].name, "new"));
    assert_runtime_state_cleared(&fixture.engine);

    reset_recording();
    assert(sc_game_engine_on_key(&fixture.engine, 'f', true));
    tick(&fixture);
    down = find_last_touch(pointer_id, AMOTION_EVENT_ACTION_DOWN);
    assert(down);
    assert(down->inject_touch_event.position.point.x == (int32_t) (0.80f * 1920));
    assert(down->inject_touch_event.position.point.y == (int32_t) (0.75f * 1080));

    fixture_destroy(&fixture);
}

static void
test_landscape_joystick_radius_uses_frame_height(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "joystick_geometry");
    add_joystick(&profile, 0.20f, 0.50f, 0.10f);

    struct fixture fixture;
    fixture_init(&fixture, &profile);
    assert(sc_game_engine_on_key(&fixture.engine, 'd', true));
    for (unsigned i = 0; i < 16; ++i) {
        tick(&fixture);
    }

    const struct sc_control_msg *move =
        find_last_touch(SC_GAME_POINTER_ID_JOYSTICK,
                        AMOTION_EVENT_ACTION_MOVE);
    assert(move);
    const int32_t center_x = (int32_t) (0.20f * 1920);
    const int32_t center_y = (int32_t) (0.50f * 1080);
    const int32_t expected_radius = (int32_t) (0.10f * 1080);
    assert(abs(move->inject_touch_event.position.point.x
               - (center_x + expected_radius)) <= 1);
    assert(abs(move->inject_touch_event.position.point.y - center_y) <= 1);

    fixture_destroy(&fixture);
}

static void
test_capture_starts_continuous_aim_and_idle_keeps_it_down(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "aim_continuous");
    add_aim_start(&profile);
    profile.presets[0].aim.enabled = true;

    struct fixture fixture;
    fixture_init(&fixture, &profile);
    assert(recorded_msg_count == 0);

    // Capture owns the camera immediately; it does not wait for the first
    // relative-motion event before establishing pointer ownership.
    sc_game_engine_set_captured(&fixture.engine, true);
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_DOWN) == 1);
    const struct sc_control_msg *down =
        find_last_touch(SC_GAME_POINTER_ID_AIM, AMOTION_EVENT_ACTION_DOWN);
    assert(down->inject_touch_event.position.point.x == (int32_t) (0.60f * 1920));
    assert(down->inject_touch_event.position.point.y == (int32_t) (0.35f * 1080));

    for (unsigned i = 0; i < 100; ++i) {
        tick(&fixture);
    }
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_DOWN) == 1);
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_UP) == 0);

    sc_game_engine_add_aim_delta(&fixture.engine, 12.0f, -7.0f);
    tick(&fixture);
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_MOVE) == 1);
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_UP) == 0);

    // Both capture transitions are idempotent and the final release is
    // paired exactly once.
    sc_game_engine_set_captured(&fixture.engine, true);
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_DOWN) == 1);
    sc_game_engine_set_captured(&fixture.engine, false);
    sc_game_engine_set_captured(&fixture.engine, false);
    sc_game_engine_release_all(&fixture.engine);
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_UP) == 1);

    fixture_destroy(&fixture);
}

static void
test_aim_edge_restarts_same_tick_and_consumes_remaining_motion(void) {
    struct sc_game_profile profile;
    init_profile(&profile, "aim_edge");
    size_t aim = add_aim_start(&profile);
    profile.presets[0].aim.enabled = true;
    profile.presets[0].elements[aim].aim.start =
        (struct sc_game_vec2) {0.90f, 0.50f};
    // An overlapping mapped button must not receive a press merely because
    // the continuous aim path passes through its visual location.
    size_t bind = add_hold(&profile, keyboard_key('e', 0), 0.92f, 0.50f);
    uint64_t bind_pointer = SC_GAME_POINTER_ID_BIND_BASE - bind;

    struct fixture fixture;
    fixture_init(&fixture, &profile);
    sc_game_engine_set_captured(&fixture.engine, true);
    reset_recording();

    // Safe right edge is 92% of 1920 = 1766.4. Starting at x=1728 and
    // moving +50 leaves 11.6px after the edge; the same tick must emit
    // edge MOVE, UP, start DOWN, then a MOVE consuming that remainder.
    sc_game_engine_add_aim_delta(&fixture.engine, 50.0f, 0.0f);
    tick(&fixture);
    assert(recorded_msg_count == 4);
    const enum android_motionevent_action actions[] = {
        AMOTION_EVENT_ACTION_MOVE,
        AMOTION_EVENT_ACTION_UP,
        AMOTION_EVENT_ACTION_DOWN,
        AMOTION_EVENT_ACTION_MOVE,
    };
    for (size_t i = 0; i < ARRAY_LEN(actions); ++i) {
        const struct sc_control_msg *msg = &recorded_msgs[i];
        assert(msg->inject_touch_event.pointer_id == SC_GAME_POINTER_ID_AIM);
        assert(msg->inject_touch_event.action == actions[i]);
        struct sc_point point = msg->inject_touch_event.position.point;
        assert(point.x >= 0 && point.x < 1920);
        assert(point.y >= 0 && point.y < 1080);
    }
    assert(recorded_msgs[0].inject_touch_event.position.point.x == 1766);
    assert(recorded_msgs[1].inject_touch_event.position.point.x == 1766);
    assert(recorded_msgs[2].inject_touch_event.position.point.x == 1728);
    assert(recorded_msgs[3].inject_touch_event.position.point.x == 1739);
    assert(count_touch(bind_pointer, AMOTION_EVENT_ACTION_DOWN) == 0);
    assert(fixture.engine.aim_touch_active);

    sc_game_engine_release_all(&fixture.engine);
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_UP) == 2);
    sc_game_engine_release_all(&fixture.engine);
    assert(count_touch(SC_GAME_POINTER_ID_AIM,
                       AMOTION_EVENT_ACTION_UP) == 2);

    fixture_destroy(&fixture);
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_key_up_after_modifier_change();
    test_ctrl_combo_modifier_first_and_no_resurrection();
    test_unrelated_modifier_release_keeps_combo_active();
    test_ctrl_combo_base_key_first();
    test_mouse_combo_lifecycle();
    test_repeat_down_does_not_stick();
    test_untracked_up_is_not_claimed_after_modifier_change();
    test_sub_tick_hold_emits_paired_touch();
    test_release_all_pairs_every_stream_and_allows_fresh_input();
    test_set_profile_releases_old_touch_before_replacement();
    test_landscape_joystick_radius_uses_frame_height();
    test_capture_starts_continuous_aim_and_idle_keeps_it_down();
    test_aim_edge_restarts_same_tick_and_consumes_remaining_motion();
    return 0;
}
