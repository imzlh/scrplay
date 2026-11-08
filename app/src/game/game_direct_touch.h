#ifndef SC_GAME_DIRECT_TOUCH_H
#define SC_GAME_DIRECT_TOUCH_H

#include "common.h"

#include <stdbool.h>

#include "controller.h"
#include "input_events.h"

// Ownership for the absolute finger controlled while the game cursor is
// temporarily free. Keeping this state outside SDL/ImGui makes DOWN/UP
// pairing independent from modifier order, capture changes and UI focus.
struct sc_game_direct_touch {
    bool active; // an Android finger DOWN has been queued without its UP
    bool awaiting_button_up; // consume the matching physical UP even if the
                             // Android finger was released by a lifecycle event
    enum sc_mouse_button button;
    struct sc_position last_position;
};

void
sc_game_direct_touch_init(struct sc_game_direct_touch *touch);

// Queue DOWN and acquire ownership. A stale physical ownership left by focus
// loss is replaced by a new DOWN. Return false if already active or if the
// message could not be queued.
bool
sc_game_direct_touch_press(struct sc_game_direct_touch *touch,
                           struct sc_controller *controller,
                           enum sc_mouse_button button,
                           struct sc_position position);

// Queue MOVE and update the last position. Return true whenever the event is
// owned (also if the droppable MOVE could not be queued).
bool
sc_game_direct_touch_move(struct sc_game_direct_touch *touch,
                          struct sc_controller *controller,
                          struct sc_position position);

// Handle the matching physical button UP. Return true if it belongs to this
// touch. If an Android UP cannot be queued, ownership is retained for a later
// lifecycle retry.
bool
sc_game_direct_touch_button_up(struct sc_game_direct_touch *touch,
                               struct sc_controller *controller,
                               enum sc_mouse_button button);

// Best-effort lifecycle release (focus loss, edit/native transition, exit).
// The later physical UP is still consumed. Return false only if an active UP
// could not be queued.
bool
sc_game_direct_touch_release(struct sc_game_direct_touch *touch,
                             struct sc_controller *controller);

#endif
