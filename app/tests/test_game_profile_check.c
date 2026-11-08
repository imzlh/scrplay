#include "common.h"

#include <assert.h>
#include <string.h>

#include "game/game_profile.h"
#include "game/game_profile_check.h"

static bool
has_issue(const struct sc_game_profile_issue *issues, size_t count,
          enum sc_game_profile_issue_code code, size_t preset,
          size_t element) {
    for (size_t i = 0; i < count; ++i) {
        if (issues[i].code == code && issues[i].preset_index == preset
                && issues[i].element_index == element) {
            return true;
        }
    }
    return false;
}

static void
test_default_is_playable(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "default");
    struct sc_game_profile_issue issues[16];
    struct sc_game_profile_check_result result =
        sc_game_profile_check(&profile, issues, ARRAY_LEN(issues));
    assert(result.issue_count == 0);
    assert(result.warning_count == 0);
    assert(result.error_count == 0);
}

static void
test_unbound_keys(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "default");
    struct sc_game_element *joy = &profile.presets[0].elements[0];
    memset(&joy->joystick.up, 0, sizeof(joy->joystick.up));
    memset(&joy->joystick.down, 0, sizeof(joy->joystick.down));
    memset(&profile.presets[0].elements[1].bind.key, 0,
           sizeof(profile.presets[0].elements[1].bind.key));
    memset(&profile.presets[1].elements[0].swipe.key, 0,
           sizeof(profile.presets[1].elements[0].swipe.key));

    struct sc_game_profile_issue issues[16];
    struct sc_game_profile_check_result result =
        sc_game_profile_check(&profile, issues, ARRAY_LEN(issues));
    assert(result.issue_count == 4);
    assert(result.error_count == 4);
    assert(result.warning_count == 0);
    assert(issues[0].key_slot == SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_UP);
    assert(issues[1].key_slot == SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_DOWN);
    assert(has_issue(issues, result.issue_count,
                     SC_GAME_PROFILE_ISSUE_UNBOUND_KEY, 0, 1));
    assert(has_issue(issues, result.issue_count,
                     SC_GAME_PROFILE_ISSUE_UNBOUND_KEY, 1, 0));
}

static void
test_duplicate_key(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "default");
    profile.presets[0].elements[1].bind.key =
        profile.presets[0].elements[0].joystick.up;

    struct sc_game_profile_issue issue;
    struct sc_game_profile_check_result result =
        sc_game_profile_check(&profile, &issue, 1);
    assert(result.issue_count == 1);
    assert(result.warning_count == 1);
    assert(issue.code == SC_GAME_PROFILE_ISSUE_DUPLICATE_KEY);
    assert(issue.element_index == 1);
    assert(issue.key_slot == SC_GAME_PROFILE_KEY_SLOT_TRIGGER);
    assert(issue.related_element_index == 0);
    assert(issue.related_key_slot == SC_GAME_PROFILE_KEY_SLOT_JOYSTICK_UP);
}

static void
test_singleton_controls(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "default");
    struct sc_game_preset *preset = &profile.presets[0];
    preset->elements[preset->element_count] = preset->elements[0];
    ++preset->element_count;
    preset->elements[preset->element_count] = preset->elements[9];
    ++preset->element_count;

    struct sc_game_profile_issue issues[32];
    struct sc_game_profile_check_result result =
        sc_game_profile_check(&profile, issues, ARRAY_LEN(issues));
    assert(result.error_count == 2);
    assert(has_issue(issues, result.issue_count,
                     SC_GAME_PROFILE_ISSUE_MULTIPLE_JOYSTICKS, 0, 10));
    assert(has_issue(issues, result.issue_count,
                     SC_GAME_PROFILE_ISSUE_MULTIPLE_AIM_REGIONS, 0, 11));
}

static void
test_aim_consistency(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "default");
    profile.presets[0].aim.enabled = false;

    struct sc_game_profile_issue issue;
    struct sc_game_profile_check_result result =
        sc_game_profile_check(&profile, &issue, 1);
    assert(result.issue_count == 1);
    assert(issue.code == SC_GAME_PROFILE_ISSUE_AIM_REGION_DISABLED);
    assert(issue.severity == SC_GAME_PROFILE_ISSUE_WARNING);

    sc_game_profile_init_default(&profile, "default");
    --profile.presets[0].element_count; // the default aim region is last
    result = sc_game_profile_check(&profile, &issue, 1);
    assert(result.issue_count == 1);
    assert(issue.code == SC_GAME_PROFILE_ISSUE_AIM_ENABLED_WITHOUT_REGION);
}

static void
test_swipe_timing_and_truncation(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "default");
    struct sc_game_element *swipe = &profile.presets[1].elements[0];
    swipe->swipe.every_ms = swipe->swipe.dur_ms - 1;

    struct sc_game_profile_check_result result =
        sc_game_profile_check(&profile, NULL, 0);
    assert(result.issue_count == 1);
    assert(result.warning_count == 1);

    struct sc_game_profile_issue issue;
    result = sc_game_profile_check(&profile, &issue, 0);
    assert(result.issue_count == 1);
    result = sc_game_profile_check(&profile, &issue, 1);
    assert(issue.code
           == SC_GAME_PROFILE_ISSUE_SWIPE_PERIOD_SHORTER_THAN_DURATION);
}

int
main(int argc, char **argv) {
    (void) argc;
    (void) argv;
    test_default_is_playable();
    test_unbound_keys();
    test_duplicate_key();
    test_singleton_controls();
    test_aim_consistency();
    test_swipe_timing_and_truncation();
    return 0;
}
