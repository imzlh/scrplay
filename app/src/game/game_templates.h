#ifndef SC_GAME_TEMPLATES_H
#define SC_GAME_TEMPLATES_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game_profile.h"

// Preset templates are deliberately independent from the UI. The editor may
// present this metadata in cards, a combo box or a first-run wizard without
// duplicating template names, descriptions or mappings.
enum sc_game_template_id {
    SC_GAME_TEMPLATE_EMPTY,
    SC_GAME_TEMPLATE_OPEN_WORLD,
    SC_GAME_TEMPLATE_DRIVING,
    SC_GAME_TEMPLATE_TURN_BASED,
    SC_GAME_TEMPLATE_SHOOTER,
    SC_GAME_TEMPLATE_MOBA,
    SC_GAME_TEMPLATE_PLATFORMER,
    SC_GAME_TEMPLATE_RHYTHM,
    SC_GAME_TEMPLATE_COUNT,
};

enum sc_game_template_feature {
    SC_GAME_TEMPLATE_FEATURE_MOVEMENT = 1 << 0,
    SC_GAME_TEMPLATE_FEATURE_ACTIONS = 1 << 1,
    SC_GAME_TEMPLATE_FEATURE_AIM = 1 << 2,
    SC_GAME_TEMPLATE_FEATURE_GESTURE = 1 << 3,
};

struct sc_game_template_info {
    enum sc_game_template_id id;
    const char *name;
    const char *description;
    const char *recommended_for;
    const char *controls; // concise default-key summary for the picker
    uint32_t features; // OR of SC_GAME_TEMPLATE_FEATURE_*
};

size_t
sc_game_template_count(void);

// Return template metadata by list index, or NULL if index is out of range.
const struct sc_game_template_info *
sc_game_template_at(size_t index);

// Return template metadata by stable identifier, or NULL for an invalid id.
const struct sc_game_template_info *
sc_game_template_get(enum sc_game_template_id id);

// Build one standalone preset. On failure, leave *preset unchanged.
bool
sc_game_template_build(enum sc_game_template_id id,
                       struct sc_game_preset *preset);

// Append a template to a valid profile and select it. Its name is made unique
// within the profile (for example, "驾驶2"). On failure, leave the profile
// unchanged. If preset_index is non-NULL, receive the appended index.
bool
sc_game_template_append(struct sc_game_profile *profile,
                        enum sc_game_template_id id, size_t *preset_index);

// Replace one preset in a valid profile and select it. The generated name is
// made unique among the other presets. On failure, leave the profile
// unchanged.
bool
sc_game_template_replace(struct sc_game_profile *profile, size_t preset_index,
                         enum sc_game_template_id id);

#endif
