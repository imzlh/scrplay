#include "common.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "android/input.h"
#include "control_msg.h"
#include "game/game_direct_touch.h"

#define MAX_RECORDED_MSGS 32

static struct sc_control_msg recorded[MAX_RECORDED_MSGS];
static size_t recorded_count;
static bool reject_push;

bool
sc_controller_push_msg(struct sc_controller *controller,
                       const struct sc_control_msg *msg) {
    (void) controller;
    if (reject_push) {
        return false;
    }
    assert(recorded_count < ARRAY_LEN(recorded));
    recorded[recorded_count++] = *msg;
    return true;
}

static struct sc_position
position(int32_t x, int32_t y) {
    return (struct sc_position) {
        .screen_size = {1920, 1080},
        .point = {x, y},
    };
}

static void
assert_touch(size_t index, enum android_motionevent_action action,
             int32_t x, int32_t y) {
    assert(index < recorded_count);
    const struct sc_control_msg *msg = &recorded[index];
    assert(msg->type == SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT);
    assert(msg->inject_touch_event.pointer_id
           == SC_POINTER_ID_GENERIC_FINGER);
    assert(msg->inject_touch_event.action == action);
    assert(msg->inject_touch_event.action_button == 0);
    assert(msg->inject_touch_event.buttons == 0);
    assert(msg->inject_touch_event.position.screen_size.width == 1920);
    assert(msg->inject_touch_event.position.screen_size.height == 1080);
    assert(msg->inject_touch_event.position.point.x == x);
    assert(msg->inject_touch_event.position.point.y == y);
    assert(msg->inject_touch_event.pressure
           == (action == AMOTION_EVENT_ACTION_UP ? 0.0f : 1.0f));
}

static void
reset_recording(void) {
    memset(recorded, 0, sizeof(recorded));
    recorded_count = 0;
    reject_push = false;
}

static void
test_press_move_release(void) {
    reset_recording();
    struct sc_game_direct_touch touch;
    sc_game_direct_touch_init(&touch);

    assert(sc_game_direct_touch_press(&touch, NULL, SC_MOUSE_BUTTON_LEFT,
                                      position(100, 200)));
    assert(touch.active && touch.awaiting_button_up);
    assert(!sc_game_direct_touch_press(&touch, NULL, SC_MOUSE_BUTTON_LEFT,
                                       position(300, 400)));
    assert(sc_game_direct_touch_move(&touch, NULL, position(120, 230)));
    assert(!sc_game_direct_touch_button_up(&touch, NULL,
                                           SC_MOUSE_BUTTON_RIGHT));
    assert(sc_game_direct_touch_button_up(&touch, NULL,
                                          SC_MOUSE_BUTTON_LEFT));
    assert(!touch.active && !touch.awaiting_button_up);
    assert(!sc_game_direct_touch_button_up(&touch, NULL,
                                           SC_MOUSE_BUTTON_LEFT));

    assert(recorded_count == 3);
    assert_touch(0, AMOTION_EVENT_ACTION_DOWN, 100, 200);
    assert_touch(1, AMOTION_EVENT_ACTION_MOVE, 120, 230);
    assert_touch(2, AMOTION_EVENT_ACTION_UP, 120, 230);
}

static void
test_lifecycle_release_consumes_late_up(void) {
    reset_recording();
    struct sc_game_direct_touch touch;
    sc_game_direct_touch_init(&touch);

    assert(sc_game_direct_touch_press(&touch, NULL, SC_MOUSE_BUTTON_LEFT,
                                      position(11, 22)));
    assert(sc_game_direct_touch_release(&touch, NULL));
    assert(!touch.active && touch.awaiting_button_up);
    assert(sc_game_direct_touch_button_up(&touch, NULL,
                                          SC_MOUSE_BUTTON_LEFT));
    assert(!touch.awaiting_button_up);
    assert(recorded_count == 2);
    assert_touch(0, AMOTION_EVENT_ACTION_DOWN, 11, 22);
    assert_touch(1, AMOTION_EVENT_ACTION_UP, 11, 22);
}

static void
test_queue_failures_retain_consistent_ownership(void) {
    reset_recording();
    struct sc_game_direct_touch touch;
    sc_game_direct_touch_init(&touch);

    reject_push = true;
    assert(!sc_game_direct_touch_press(&touch, NULL, SC_MOUSE_BUTTON_LEFT,
                                       position(1, 2)));
    assert(!touch.active && !touch.awaiting_button_up);

    reject_push = false;
    assert(sc_game_direct_touch_press(&touch, NULL, SC_MOUSE_BUTTON_LEFT,
                                      position(3, 4)));
    reject_push = true;
    assert(sc_game_direct_touch_move(&touch, NULL, position(30, 40)));
    assert(touch.last_position.point.x == 3);
    assert(touch.last_position.point.y == 4);
    assert(!sc_game_direct_touch_release(&touch, NULL));
    assert(touch.active && touch.awaiting_button_up);

    // The physical UP retries the non-droppable Android boundary.
    reject_push = false;
    assert(sc_game_direct_touch_button_up(&touch, NULL,
                                          SC_MOUSE_BUTTON_LEFT));
    assert(!touch.active && !touch.awaiting_button_up);
    assert(recorded_count == 2);
    assert_touch(0, AMOTION_EVENT_ACTION_DOWN, 3, 4);
    assert_touch(1, AMOTION_EVENT_ACTION_UP, 3, 4);
}

static void
test_new_down_replaces_stale_physical_ownership(void) {
    reset_recording();
    struct sc_game_direct_touch touch;
    sc_game_direct_touch_init(&touch);

    assert(sc_game_direct_touch_press(&touch, NULL, SC_MOUSE_BUTTON_LEFT,
                                      position(10, 20)));
    assert(sc_game_direct_touch_release(&touch, NULL));
    // Focus loss may prevent the old physical UP. A fresh DOWN is definitive.
    assert(sc_game_direct_touch_press(&touch, NULL, SC_MOUSE_BUTTON_LEFT,
                                      position(30, 40)));
    assert(sc_game_direct_touch_button_up(&touch, NULL,
                                          SC_MOUSE_BUTTON_LEFT));
    assert(recorded_count == 4);
    assert_touch(2, AMOTION_EVENT_ACTION_DOWN, 30, 40);
    assert_touch(3, AMOTION_EVENT_ACTION_UP, 30, 40);
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;
    test_press_move_release();
    test_lifecycle_release_consumes_late_up();
    test_queue_failures_retain_consistent_ownership();
    test_new_down_replaces_stale_physical_ownership();
    return 0;
}
