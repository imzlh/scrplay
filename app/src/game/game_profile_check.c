#include "game_profile_check.h"

#include <stdbool.h>

struct issue_collector {
    struct sc_game_profile_issue *issues;
    size_t capacity;
    struct sc_game_profile_check_result result;
};

struct binding_source {
    const struct sc_game_key *key;
    size_t element_index;
    enum sc_game_profile_key_slot slot;
};

static void
add_issue(struct issue_collector *collector,
          enum sc_game_profile_issue_severity severity,
          enum sc_game_profile_issue_code code, size_t preset_index,
          size_t element_index, enum sc_game_profile_key_slot key_slot,
          size_t related_element_index,
          enum sc_game_profile_key_slot related_key_slot) {
    if (collector->result.issue_count < collector->capacity) {
        collector->issues[collector->result.issue_count] =
            (struct sc_game_profile_issue) {
                .severity = severity,
                .code = code,
                .preset_index = preset_index,
                .element_index = element_index,
                .key_slot = key_slot,
                .related_element_index = related_element_index,
                .related_key_slot = related_key_slot,
            };
    }

    ++collector->result.issue_count;
    if (severity == SC_GAME_PROFILE_ISSUE_ERROR) {
        ++collector->result.error_count;
    } else {
        ++collector->result.warning_count;
    }
}

static bool
keys_equal(const struct sc_game_key *a, const struct sc_game_key *b) {
    return a->type == b->type && a->value == b->value && a->mods == b->mods;
}

static void
add_binding(struct issue_collector *collector,
            struct binding_source *bindings, size_t *binding_count,
            size_t preset_index, size_t element_index,
            enum sc_game_profile_key_slot slot,
            const struct sc_game_key *key) {
    if (key->type == SC_GAME_KEY_NONE) {
        add_issue(collector, SC_GAME_PROFILE_ISSUE_ERROR,
                  SC_GAME_PROFILE_ISSUE_UNBOUND_KEY, preset_index,
                  element_index, slot, SC_GAME_PROFILE_ISSUE_INDEX_NONE,
                  SC_GAME_PROFILE_KEY_SLOT_NONE);
        return;
    }

    bindings[(*binding_count)++] = (struct binding_source) {
        .key = key,
        .element_index = element_index,
        .slot = slot,
    };
}

static void
check_preset(struct issue_collector *collector,
             const struct sc_game_preset *preset, size_t preset_index) {
    struct binding_source bindings[SC_GAME_MAX_ELEMENTS * 4];
    size_t binding_count = 0;
    size_t first_joystick = SC_GAME_PROFILE_ISSUE_INDEX_NONE;
    size_t first_aim_region = SC_GAME_PROFILE_ISSUE_INDEX_NONE;
    size_t joystick_count = 0;
    size_t aim_region_count = 0;
    size_t element_count = MIN(preset->element_count, SC_GAME_MAX_ELEMENTS);

    for (size_t i = 0; i < element_count; ++i) {
        const struct sc_game_element *elem = &preset->elements[i];
        switch (elem->type) {
            case SC_GAME_ELEMENT_JOYSTICK:
                if (!joystick_count) {
                    first_joystick = i;
                } else {
                    add_issue(collector, SC_GAME_PROFILE_ISSUE_ERROR,
                              SC_GAME_PROFILE_ISSUE_MULTIPLE_JOYSTICKS,
                              preset_index, i,
                              SC_GAME_PROFILE_KEY_SLOT_NONE, first_joystick,
                              SC_GAME_PROFILE_KEY_SLOT_NONE);
                }
                ++joystick_count;
                add_binding(collector, bindings, &binding_count, preset_index,
                            i, SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_UP,
                            &elem->joystick.up);
                add_binding(collector, bindings, &binding_count, preset_index,
                            i, SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_DOWN,
                            &elem->joystick.down);
                add_binding(collector, bindings, &binding_count, preset_index,
                            i, SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_LEFT,
                            &elem->joystick.left);
                add_binding(collector, bindings, &binding_count, preset_index,
                            i, SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_RIGHT,
                            &elem->joystick.right);
                break;
            case SC_GAME_ELEMENT_BIND:
                add_binding(collector, bindings, &binding_count, preset_index,
                            i, SC_GAME_PROFILE_KEY_SLOT_TRIGGER,
                            &elem->bind.key);
                break;
            case SC_GAME_ELEMENT_SWIPE:
                add_binding(collector, bindings, &binding_count, preset_index,
                            i, SC_GAME_PROFILE_KEY_SLOT_TRIGGER,
                            &elem->swipe.key);
                if (elem->swipe.every_ms < elem->swipe.dur_ms) {
                    add_issue(
                        collector, SC_GAME_PROFILE_ISSUE_WARNING,
                        SC_GAME_PROFILE_ISSUE_SWIPE_PERIOD_SHORTER_THAN_DURATION,
                        preset_index, i, SC_GAME_PROFILE_KEY_SLOT_NONE,
                        SC_GAME_PROFILE_ISSUE_INDEX_NONE,
                        SC_GAME_PROFILE_KEY_SLOT_NONE);
                }
                break;
            case SC_GAME_ELEMENT_AIM:
                if (!aim_region_count) {
                    first_aim_region = i;
                } else {
                    add_issue(collector, SC_GAME_PROFILE_ISSUE_ERROR,
                              SC_GAME_PROFILE_ISSUE_MULTIPLE_AIM_REGIONS,
                              preset_index, i,
                              SC_GAME_PROFILE_KEY_SLOT_NONE, first_aim_region,
                              SC_GAME_PROFILE_KEY_SLOT_NONE);
                }
                ++aim_region_count;
                break;
        }
    }

    if (preset->aim.enabled && !aim_region_count) {
        add_issue(collector, SC_GAME_PROFILE_ISSUE_WARNING,
                  SC_GAME_PROFILE_ISSUE_AIM_ENABLED_WITHOUT_REGION,
                  preset_index, SC_GAME_PROFILE_ISSUE_INDEX_NONE,
                  SC_GAME_PROFILE_KEY_SLOT_NONE,
                  SC_GAME_PROFILE_ISSUE_INDEX_NONE,
                  SC_GAME_PROFILE_KEY_SLOT_NONE);
    } else if (!preset->aim.enabled && aim_region_count) {
        add_issue(collector, SC_GAME_PROFILE_ISSUE_WARNING,
                  SC_GAME_PROFILE_ISSUE_AIM_REGION_DISABLED, preset_index,
                  first_aim_region, SC_GAME_PROFILE_KEY_SLOT_NONE,
                  SC_GAME_PROFILE_ISSUE_INDEX_NONE,
                  SC_GAME_PROFILE_KEY_SLOT_NONE);
    }

    // Duplicate bindings are scoped to one preset: the same physical key may
    // reasonably perform a different action in another preset.
    for (size_t i = 0; i < binding_count; ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (keys_equal(bindings[i].key, bindings[j].key)) {
                add_issue(collector, SC_GAME_PROFILE_ISSUE_WARNING,
                          SC_GAME_PROFILE_ISSUE_DUPLICATE_KEY, preset_index,
                          bindings[i].element_index, bindings[i].slot,
                          bindings[j].element_index, bindings[j].slot);
            }
        }
    }
}

struct sc_game_profile_check_result
sc_game_profile_check(const struct sc_game_profile *profile,
                      struct sc_game_profile_issue *issues,
                      size_t issue_capacity) {
    struct issue_collector collector = {
        .issues = issues,
        .capacity = issues ? issue_capacity : 0,
        .result = {0},
    };
    if (!profile) {
        return collector.result;
    }

    size_t preset_count = MIN(profile->preset_count, SC_GAME_MAX_PRESETS);
    for (size_t i = 0; i < preset_count; ++i) {
        check_preset(&collector, &profile->presets[i], i);
    }
    return collector.result;
}
