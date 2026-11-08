#include "common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "game/game_profile.h"
#include "game/game_profile_check.h"
#include "game/game_templates.h"

static void
set_only_preset(struct sc_game_profile *profile,
                const struct sc_game_preset *preset) {
    sc_game_profile_init_default(profile, "template_test");
    profile->presets[0] = *preset;
    profile->preset_count = 1;
    profile->current_preset = 0;
}

static void
test_metadata(void) {
    assert(sc_game_template_count() == SC_GAME_TEMPLATE_COUNT);
    assert(!sc_game_template_at(sc_game_template_count()));
    assert(!sc_game_template_get((enum sc_game_template_id) -1));
    assert(!sc_game_template_get(SC_GAME_TEMPLATE_COUNT));

    for (size_t i = 0; i < sc_game_template_count(); ++i) {
        const struct sc_game_template_info *info = sc_game_template_at(i);
        assert(info);
        assert(sc_game_template_get(info->id) == info);
        assert(info->name && *info->name);
        assert(info->description && *info->description);
        assert(info->recommended_for && *info->recommended_for);
        assert(info->controls && *info->controls);
        assert(sc_game_preset_name_is_valid(info->name));
        for (size_t j = i + 1; j < sc_game_template_count(); ++j) {
            const struct sc_game_template_info *other =
                sc_game_template_at(j);
            assert(info->id != other->id);
            assert(strcmp(info->name, other->name));
        }
    }
}

static void
test_build_and_roundtrip(void) {
    for (size_t i = 0; i < sc_game_template_count(); ++i) {
        const struct sc_game_template_info *info = sc_game_template_at(i);
        struct sc_game_preset preset;
        memset(&preset, 0xA5, sizeof(preset));
        assert(sc_game_template_build(info->id, &preset));
        assert(!strcmp(preset.name, info->name));

        struct sc_game_profile profile;
        set_only_preset(&profile, &preset);
        assert(sc_game_profile_validate(&profile));

        // A template must be internally coherent: no missing or duplicate
        // bindings, conflicting singleton controls, or inconsistent aim.
        struct sc_game_profile_check_result check =
            sc_game_profile_check(&profile, NULL, 0);
        assert(check.issue_count == 0);
        assert(check.warning_count == 0);
        assert(check.error_count == 0);

        char *serialized = sc_game_profile_serialize(&profile);
        assert(serialized);
        struct sc_game_profile reparsed;
        size_t error_line = 0;
        assert(sc_game_profile_parse(&reparsed, serialized, &error_line));
        char *serialized_again = sc_game_profile_serialize(&reparsed);
        assert(serialized_again);
        assert(!strcmp(serialized, serialized_again));
        free(serialized_again);
        free(serialized);
    }

    struct sc_game_preset sentinel;
    memset(&sentinel, 0x5A, sizeof(sentinel));
    struct sc_game_preset unchanged = sentinel;
    assert(!sc_game_template_build(SC_GAME_TEMPLATE_COUNT, &sentinel));
    assert(!memcmp(&sentinel, &unchanged, sizeof(sentinel)));
}

static void
test_template_characteristics(void) {
    struct sc_game_preset preset;
    assert(sc_game_template_build(SC_GAME_TEMPLATE_EMPTY, &preset));
    assert(!preset.element_count);
    assert(!preset.aim.enabled);

    assert(sc_game_template_build(SC_GAME_TEMPLATE_DRIVING, &preset));
    assert(preset.element_count >= 6);
    assert(!preset.aim.enabled);

    assert(sc_game_template_build(SC_GAME_TEMPLATE_TURN_BASED, &preset));
    assert(preset.elements[0].type == SC_GAME_ELEMENT_SWIPE);
    assert(!preset.aim.enabled);

    assert(sc_game_template_build(SC_GAME_TEMPLATE_OPEN_WORLD, &preset));
    assert(preset.elements[0].type == SC_GAME_ELEMENT_JOYSTICK);
    assert(preset.aim.enabled);
    assert(preset.elements[preset.element_count - 1].type
           == SC_GAME_ELEMENT_AIM);
    assert(preset.elements[preset.element_count - 1].aim.start.x == 0.56f);
    assert(preset.elements[preset.element_count - 1].aim.start.y == 0.28f);

    assert(sc_game_template_build(SC_GAME_TEMPLATE_SHOOTER, &preset));
    assert(preset.elements[0].type == SC_GAME_ELEMENT_JOYSTICK);
    assert(preset.aim.enabled);
    assert(preset.elements[preset.element_count - 1].type
           == SC_GAME_ELEMENT_AIM);
    assert(preset.elements[preset.element_count - 1].aim.start.x == 0.62f);
    assert(preset.elements[preset.element_count - 1].aim.start.y == 0.31f);

    assert(sc_game_template_build(SC_GAME_TEMPLATE_MOBA, &preset));
    assert(preset.elements[0].type == SC_GAME_ELEMENT_JOYSTICK);
    assert(!preset.aim.enabled);

    assert(sc_game_template_build(SC_GAME_TEMPLATE_PLATFORMER, &preset));
    assert(preset.elements[0].type == SC_GAME_ELEMENT_JOYSTICK);
    assert(!preset.aim.enabled);

    assert(sc_game_template_build(SC_GAME_TEMPLATE_RHYTHM, &preset));
    assert(preset.element_count == 4);
    assert(!preset.aim.enabled);
    assert(!preset.humanize.enabled);
    for (size_t i = 0; i < preset.element_count; ++i) {
        assert(preset.elements[i].type == SC_GAME_ELEMENT_BIND);
        assert(preset.elements[i].bind.action == SC_GAME_ACTION_HOLD);
    }
}

static void
test_append_and_unique_names(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "templates");
    assert(sc_game_profile_validate(&profile));

    while (profile.preset_count < SC_GAME_MAX_PRESETS) {
        size_t previous_count = profile.preset_count;
        size_t index = SIZE_MAX;
        assert(sc_game_template_append(&profile, SC_GAME_TEMPLATE_DRIVING,
                                       &index));
        assert(index == previous_count);
        assert(profile.current_preset == index);
        assert(sc_game_profile_validate(&profile));
        for (size_t i = 0; i < profile.preset_count; ++i) {
            for (size_t j = i + 1; j < profile.preset_count; ++j) {
                assert(strcmp(profile.presets[i].name,
                              profile.presets[j].name));
            }
        }
    }

    struct sc_game_profile full = profile;
    size_t index = 123;
    assert(!sc_game_template_append(&profile, SC_GAME_TEMPLATE_EMPTY,
                                    &index));
    assert(index == 123);
    assert(!memcmp(&profile, &full, sizeof(profile)));

    assert(!sc_game_template_append(&profile, SC_GAME_TEMPLATE_COUNT, NULL));
    assert(!memcmp(&profile, &full, sizeof(profile)));
}

static void
test_replace_and_failure_atomicity(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "templates");
    assert(sc_game_profile_validate(&profile));

    assert(sc_game_template_replace(&profile, 0, SC_GAME_TEMPLATE_SHOOTER));
    assert(profile.current_preset == 0);
    assert(!strcmp(profile.presets[0].name, "射击"));
    assert(profile.presets[0].aim.enabled);
    assert(sc_game_profile_validate(&profile));

    struct sc_game_profile unchanged = profile;
    assert(!sc_game_template_replace(&profile, profile.preset_count,
                                     SC_GAME_TEMPLATE_EMPTY));
    assert(!memcmp(&profile, &unchanged, sizeof(profile)));
    assert(!sc_game_template_replace(&profile, 0, SC_GAME_TEMPLATE_COUNT));
    assert(!memcmp(&profile, &unchanged, sizeof(profile)));

    // Invalid input profiles are rejected rather than partially repaired.
    profile.current_preset = profile.preset_count;
    unchanged = profile;
    assert(!sc_game_template_replace(&profile, 0, SC_GAME_TEMPLATE_EMPTY));
    assert(!memcmp(&profile, &unchanged, sizeof(profile)));
}

int
main(int argc, char **argv) {
    (void) argc;
    (void) argv;
    test_metadata();
    test_build_and_roundtrip();
    test_template_characteristics();
    test_append_and_unique_names();
    test_replace_and_failure_atomicity();
    return 0;
}
