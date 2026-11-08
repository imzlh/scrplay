#ifndef SC_GAME_PROFILE_CHECK_H
#define SC_GAME_PROFILE_CHECK_H

#include "common.h"

#include <stddef.h>

#include "game_profile.h"

// These values are part of the UI-facing diagnostic contract. Keep existing
// numeric values stable when adding new diagnostics.
enum sc_game_profile_issue_severity {
    SC_GAME_PROFILE_ISSUE_WARNING = 1,
    SC_GAME_PROFILE_ISSUE_ERROR = 2,
};

enum sc_game_profile_issue_code {
    SC_GAME_PROFILE_ISSUE_UNBOUND_KEY = 1,
    SC_GAME_PROFILE_ISSUE_DUPLICATE_KEY = 2,
    SC_GAME_PROFILE_ISSUE_MULTIPLE_JOYSTICKS = 3,
    SC_GAME_PROFILE_ISSUE_MULTIPLE_AIM_REGIONS = 4,
    SC_GAME_PROFILE_ISSUE_AIM_ENABLED_WITHOUT_REGION = 5,
    SC_GAME_PROFILE_ISSUE_AIM_REGION_DISABLED = 6,
    SC_GAME_PROFILE_ISSUE_SWIPE_PERIOD_SHORTER_THAN_DURATION = 7,
};

// Identifies the binding inside an element. TRIGGER is used by bind/swipe;
// NONE is used by diagnostics which concern the whole element or preset.
enum sc_game_profile_key_slot {
    SC_GAME_PROFILE_KEY_SLOT_NONE = 0,
    SC_GAME_PROFILE_KEY_SLOT_TRIGGER = 1,
    SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_UP = 2,
    SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_DOWN = 3,
    SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_LEFT = 4,
    SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_RIGHT = 5,
};

#define SC_GAME_PROFILE_ISSUE_INDEX_NONE SIZE_MAX

struct sc_game_profile_issue {
    enum sc_game_profile_issue_severity severity;
    enum sc_game_profile_issue_code code;
    size_t preset_index;
    size_t element_index;
    enum sc_game_profile_key_slot key_slot;

    // Only populated for diagnostics which relate two elements/bindings
    // (duplicate key and multiple singleton controls).
    size_t related_element_index;
    enum sc_game_profile_key_slot related_key_slot;
};

struct sc_game_profile_check_result {
    size_t issue_count;
    size_t warning_count;
    size_t error_count;
};

// Check whether an otherwise structurally valid profile is practical to use.
// This intentionally complements sc_game_profile_validate(): it reports
// playable-but-surprising setups instead of serialization/schema errors.
//
// At most issue_capacity entries are written, but issue_count always reports
// the total number found. It is therefore valid to pass NULL, 0 to query the
// required capacity first.
struct sc_game_profile_check_result
sc_game_profile_check(const struct sc_game_profile *profile,
                      struct sc_game_profile_issue *issues,
                      size_t issue_capacity);

#endif
