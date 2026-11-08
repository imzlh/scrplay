#include "game_direct_touch.h"

#include <string.h>

#include "android/input.h"
#include "control_msg.h"

static bool
push_touch(struct sc_controller *controller,
           enum android_motionevent_action action,
           struct sc_position position) {
    struct sc_control_msg msg = {
        .type = SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT,
        .inject_touch_event = {
            .action = action,
            .action_button = 0,
            .buttons = 0,
            .pointer_id = SC_POINTER_ID_GENERIC_FINGER,
            .position = position,
            .pressure = action == AMOTION_EVENT_ACTION_UP ? 0.0f : 1.0f,
        },
    };
    return sc_controller_push_msg(controller, &msg);
}

void
sc_game_direct_touch_init(struct sc_game_direct_touch *touch) {
    memset(touch, 0, sizeof(*touch));
    touch->button = SC_MOUSE_BUTTON_UNKNOWN;
}

bool
sc_game_direct_touch_press(struct sc_game_direct_touch *touch,
                           struct sc_controller *controller,
                           enum sc_mouse_button button,
                           struct sc_position position) {
    if (touch->active || button == SC_MOUSE_BUTTON_UNKNOWN) {
        return false;
    }

    // A fresh physical DOWN proves that a stale UP lost during focus loss is
    // no longer forthcoming.
    touch->awaiting_button_up = false;
    touch->button = SC_MOUSE_BUTTON_UNKNOWN;
    if (!push_touch(controller, AMOTION_EVENT_ACTION_DOWN, position)) {
        return false;
    }

    touch->active = true;
    touch->awaiting_button_up = true;
    touch->button = button;
    touch->last_position = position;
    return true;
}

bool
sc_game_direct_touch_move(struct sc_game_direct_touch *touch,
                          struct sc_controller *controller,
                          struct sc_position position) {
    if (!touch->active) {
        return false;
    }

    if (push_touch(controller, AMOTION_EVENT_ACTION_MOVE, position)) {
        touch->last_position = position;
    }
    return true;
}

bool
sc_game_direct_touch_button_up(struct sc_game_direct_touch *touch,
                               struct sc_controller *controller,
                               enum sc_mouse_button button) {
    if (!touch->awaiting_button_up || button != touch->button) {
        return false;
    }

    if (touch->active
            && !push_touch(controller, AMOTION_EVENT_ACTION_UP,
                           touch->last_position)) {
        return true;
    }

    touch->active = false;
    touch->awaiting_button_up = false;
    touch->button = SC_MOUSE_BUTTON_UNKNOWN;
    return true;
}

bool
sc_game_direct_touch_release(struct sc_game_direct_touch *touch,
                             struct sc_controller *controller) {
    if (!touch->active) {
        return true;
    }
    if (!push_touch(controller, AMOTION_EVENT_ACTION_UP,
                    touch->last_position)) {
        return false;
    }
    touch->active = false;
    return true;
}
