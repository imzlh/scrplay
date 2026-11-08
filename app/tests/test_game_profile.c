#include "common.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game_profile.h"
#include "input_events.h"

static void
test_parse_default(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "default");

    assert(profile.preset_count == 2);
    assert(profile.presets[0].element_count == 10);
    assert(profile.presets[1].element_count == 3);
    assert(profile.presets[1].elements[0].type == SC_GAME_ELEMENT_SWIPE);
    assert(profile.presets[0].aim.enabled);
    // aim start point closes the default template
    const struct sc_game_element *aim =
        &profile.presets[0].elements[9];
    assert(aim->type == SC_GAME_ELEMENT_AIM);
    assert(aim->aim.start.x == 0.550f);
    assert(aim->aim.start.y == 0.250f);
    const struct sc_game_element *joy = &profile.presets[0].elements[0];
    assert(joy->type == SC_GAME_ELEMENT_JOYSTICK);
    struct sc_game_key wkey;
    assert(sc_game_key_from_name("w", &wkey));
    assert(joy->joystick.up.value == wkey.value);
    assert(!profile.crop.enabled);
    assert(profile.crop.left == 0.0f);
    assert(profile.crop.top == 0.0f);
    assert(profile.crop.right == 1.0f);
    assert(profile.crop.bottom == 1.0f);
    assert(sc_game_profile_validate(&profile));
}

static void
test_name_validation(void) {
    assert(sc_game_profile_name_is_valid("default"));
    assert(sc_game_profile_name_is_valid("原神.测试"));
    assert(!sc_game_profile_name_is_valid(NULL));
    assert(!sc_game_profile_name_is_valid(""));
    assert(!sc_game_profile_name_is_valid("."));
    assert(!sc_game_profile_name_is_valid(".."));
    assert(!sc_game_profile_name_is_valid("two words"));
    assert(!sc_game_profile_name_is_valid("../escape"));
    assert(!sc_game_profile_name_is_valid("dir\\escape"));
    assert(!sc_game_profile_name_is_valid("line\nbreak"));

    char too_long_profile[SC_GAME_PROFILE_NAME_MAX + 1];
    memset(too_long_profile, 'x', sizeof(too_long_profile));
    too_long_profile[sizeof(too_long_profile) - 1] = '\0';
    assert(!sc_game_profile_name_is_valid(too_long_profile));

    assert(sc_game_preset_name_is_valid("默认"));
    assert(sc_game_preset_name_is_valid("preset/1"));
    assert(!sc_game_preset_name_is_valid(NULL));
    assert(!sc_game_preset_name_is_valid(""));
    assert(!sc_game_preset_name_is_valid("two words"));
    assert(!sc_game_preset_name_is_valid("tab\tname"));
}

static void
test_model_validation(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "default");
    assert(sc_game_profile_validate(&profile));

    profile.current_preset = profile.preset_count;
    assert(!sc_game_profile_validate(&profile));
    assert(!sc_game_profile_serialize(&profile));
    profile.current_preset = 0;

    struct sc_game_element *bind = &profile.presets[0].elements[1];
    float saved_size = bind->bind.size;
    bind->bind.size = NAN;
    assert(!sc_game_profile_validate(&profile));
    bind->bind.size = INFINITY;
    assert(!sc_game_profile_validate(&profile));
    bind->bind.size = saved_size;

    float saved_x = bind->bind.center.x;
    bind->bind.center.x = -0.001f;
    assert(!sc_game_profile_validate(&profile));
    bind->bind.center.x = saved_x;

    char saved_label[SC_GAME_ELEMENT_LABEL_MAX];
    snprintf(saved_label, sizeof(saved_label), "%s", bind->label);
    snprintf(bind->label, sizeof(bind->label), " bad");
    assert(!sc_game_profile_validate(&profile));
    snprintf(bind->label, sizeof(bind->label), "bad  label");
    assert(!sc_game_profile_validate(&profile));
    snprintf(bind->label, sizeof(bind->label), "%s", saved_label);

    struct sc_game_key saved_key = bind->bind.key;
    bind->bind.key.type = SC_GAME_KEY_KEYBOARD;
    bind->bind.key.value = 0xFFFFFFFFu;
    assert(!sc_game_profile_validate(&profile));
    bind->bind.key = saved_key;
    assert(sc_game_profile_validate(&profile));

    struct sc_game_crop saved_crop = profile.crop;
    profile.crop.left = NAN;
    assert(!sc_game_profile_validate(&profile));
    profile.crop = saved_crop;
    profile.crop.right = profile.crop.left;
    assert(!sc_game_profile_validate(&profile));
    profile.crop = saved_crop;
    profile.crop.right = profile.crop.left + SC_GAME_CROP_MIN_SIZE * 0.5f;
    assert(!sc_game_profile_validate(&profile));
    profile.crop = saved_crop;
    profile.crop.left = 0.1f;
    assert(!sc_game_profile_validate(&profile));
    profile.crop = saved_crop;
    assert(sc_game_profile_validate(&profile));
}

static void
test_unbound_keys_roundtrip(void) {
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "unbound");

    struct sc_game_element *joy = &profile.presets[0].elements[0];
    memset(&joy->joystick.up, 0, sizeof(joy->joystick.up));
    memset(&joy->joystick.down, 0, sizeof(joy->joystick.down));
    memset(&joy->joystick.left, 0, sizeof(joy->joystick.left));
    memset(&joy->joystick.right, 0, sizeof(joy->joystick.right));

    struct sc_game_element *bind = &profile.presets[0].elements[1];
    memset(&bind->bind.key, 0, sizeof(bind->bind.key));

    struct sc_game_element *swipe = &profile.presets[1].elements[0];
    memset(&swipe->swipe.key, 0, sizeof(swipe->swipe.key));

    assert(sc_game_profile_validate(&profile));
    char *serialized = sc_game_profile_serialize(&profile);
    assert(serialized);
    assert(strstr(serialized, "up=none down=none left=none right=none"));
    assert(strstr(serialized, "bind key=none"));
    assert(strstr(serialized, "swipe key=none"));
    assert(!strncmp(serialized, "format 2\n", strlen("format 2\n")));
    assert(strstr(serialized, "crop off\n"));

    struct sc_game_profile reparsed;
    size_t error_line = 0;
    assert(sc_game_profile_parse(&reparsed, serialized, &error_line));
    assert(reparsed.presets[0].elements[0].joystick.up.type
           == SC_GAME_KEY_NONE);
    assert(reparsed.presets[0].elements[0].joystick.down.type
           == SC_GAME_KEY_NONE);
    assert(reparsed.presets[0].elements[0].joystick.left.type
           == SC_GAME_KEY_NONE);
    assert(reparsed.presets[0].elements[0].joystick.right.type
           == SC_GAME_KEY_NONE);
    assert(reparsed.presets[0].elements[1].bind.key.type == SC_GAME_KEY_NONE);
    assert(reparsed.presets[1].elements[0].swipe.key.type == SC_GAME_KEY_NONE);
    assert(sc_game_profile_validate(&reparsed));
    free(serialized);

    struct sc_game_key key;
    assert(sc_game_key_from_name("none", &key));
    assert(key.type == SC_GAME_KEY_NONE);
    assert(!sc_game_key_from_name("-", &key));
}

static void
test_parse_serialize_roundtrip(void) {
    const char *text =
        "# comment\n"
        "format 2\n"
        "profile 原神\n"
        "hotkey edit=shift+e preset=f8\n"
        "crop rect=0.055556,0.000000,0.944444,1.000000\n"
        "\n"
        "preset 默认\n"
        "aim on sens=1.50\n"
        "joystick center=0.10,0.80 radius=0.08 up=w down=s left=a right=d"
        " label=移动\n"
        "bind key=lmb action=tap pos=0.88,0.72 size=0.10 label=普攻\n"
        "bind key=shift action=hold pos=0.30,0.90 size=0.08 label=冲刺\n"
        "aim start=0.60,0.42 label=瞄准\n"
        "\n"
        "humanize on amp=0.0040\n"
        "swipe key=ctrl+w from=0.50,0.62 to=0.50,0.42 dur=300 every=520"
        " label=走两步\n"
        "bind key=space action=hold pos=0.60,0.90 size=0.08 label=跳 跃\n"
        "\n"
        "preset 备用\n"
        "aim off\n"
        "humanize off\n"
        "bind key=f1 action=tap pos=0.92,0.06 size=0.05\n";

    struct sc_game_profile profile;
    size_t error_line;
    bool ok = sc_game_profile_parse(&profile, text, &error_line);
    assert(ok);
    assert(!strcmp(profile.name, "原神"));
    assert(profile.preset_count == 2);
    assert(profile.current_preset == 0);
    assert(profile.crop.enabled);
    assert(fabsf(profile.crop.left - 0.055556f) < 0.000001f);
    assert(profile.crop.top == 0.0f);
    assert(fabsf(profile.crop.right - 0.944444f) < 0.000001f);
    assert(profile.crop.bottom == 1.0f);
    assert(profile.presets[0].element_count == 6);
    // aim start-point element
    const struct sc_game_element *ar = &profile.presets[0].elements[3];
    assert(ar->type == SC_GAME_ELEMENT_AIM);
    assert(ar->aim.start.x == 0.60f);
    assert(ar->aim.start.y == 0.42f);
    assert(!strcmp(ar->label, "瞄准"));
    assert(profile.presets[0].aim.enabled);
    assert(profile.presets[0].aim.sensitivity == 1.5f);
    assert(profile.presets[0].humanize.enabled);
    assert(profile.presets[0].humanize.amp == 0.004f);
    assert(profile.presets[1].element_count == 1);
    assert(!profile.presets[1].aim.enabled);
    assert(!profile.presets[1].humanize.enabled);

    // swipe element
    const struct sc_game_element *sw = &profile.presets[0].elements[4];
    assert(sw->type == SC_GAME_ELEMENT_SWIPE);
    assert(!strcmp(sw->label, "走两步"));
    assert(sw->swipe.key.mods == SC_GAME_MOD_CTRL);
    assert(sw->swipe.key.value == 'w');
    assert(sw->swipe.dur_ms == 300);
    assert(sw->swipe.every_ms == 520);

    // label containing spaces survives
    const struct sc_game_element *sp = &profile.presets[0].elements[5];
    assert(sp->type == SC_GAME_ELEMENT_BIND);
    assert(!strcmp(sp->label, "跳 跃"));


    // hotkeys
    assert(profile.hotkey_edit.shift);
    assert(profile.hotkey_edit.key.value == 'e');
    char hk[32];
    sc_game_hotkey_to_name(&profile.hotkey_edit, hk, sizeof(hk));
    assert(!strcmp(hk, "shift+e"));
    struct sc_game_key f8;
    assert(sc_game_key_from_name("f8", &f8));
    assert(profile.hotkey_next_preset.key.value == f8.value);

    // key conversions
    struct sc_game_key key;
    assert(sc_game_key_from_name("lmb", &key));
    assert(key.type == SC_GAME_KEY_MOUSE);
    assert(sc_game_key_from_name("F1", &key));
    assert(key.type == SC_GAME_KEY_KEYBOARD);
    assert(sc_game_key_to_android(&key) != 0);
    char name[32];
    sc_game_key_to_name(&key, name, sizeof(name));
    assert(!strcmp(name, "f1"));
    // combo keys
    assert(sc_game_key_from_name("ctrl+1", &key));
    assert(key.type == SC_GAME_KEY_KEYBOARD);
    assert(key.value == '1');
    assert(key.mods == SC_GAME_MOD_CTRL);
    assert(sc_game_key_from_name("ctrl+shift+e", &key));
    assert(key.mods == (SC_GAME_MOD_CTRL | SC_GAME_MOD_SHIFT));
    char cname[32];
    sc_game_key_to_name(&key, cname, sizeof(cname));
    assert(!strcmp(cname, "ctrl+shift+e"));
    assert(!sc_game_key_from_name("nosuchkey", &key));
    assert(!sc_game_key_from_name("ctrl+", &key));

    // mouse buttons match engine-side values
    assert(sc_game_key_from_name("m4", &key));
    assert(key.value == SC_MOUSE_BUTTON_X1);

    // roundtrip
    char *serialized = sc_game_profile_serialize(&profile);
    assert(serialized);
    assert(strstr(serialized, "aim start=0.6000,0.4200 label=瞄准\n"));
    assert(strstr(serialized,
                  "crop rect=0.055556,0.000000,0.944444,1.000000\n"));
    assert(!strstr(serialized, "aim center="));
    assert(!strstr(serialized, "radius=0.1500"));
    struct sc_game_profile reparsed;
    ok = sc_game_profile_parse(&reparsed, serialized, &error_line);
    assert(ok);
    assert(!strcmp(reparsed.name, profile.name));
    assert(reparsed.preset_count == profile.preset_count);
    assert(reparsed.presets[0].element_count
           == profile.presets[0].element_count);
    assert(reparsed.presets[0].elements[1].bind.action
           == profile.presets[0].elements[1].bind.action);
    float dx = reparsed.presets[0].elements[1].bind.center.x
             - profile.presets[0].elements[1].bind.center.x;
    assert(dx > -0.0001f && dx < 0.0001f);
    assert(reparsed.hotkey_edit.key.value == profile.hotkey_edit.key.value);
    assert(reparsed.crop.enabled);
    assert(fabsf(reparsed.crop.left - profile.crop.left) < 0.000001f);
    assert(fabsf(reparsed.crop.right - profile.crop.right) < 0.000001f);
    free(serialized);
}

static void
test_parse_errors(void) {
    struct sc_game_profile profile;
    size_t line;

    // missing profile name
    bool ok = sc_game_profile_parse(&profile, "format 2\npreset p\n", &line);
    assert(!ok);
    assert(line == 2);

    // element before any preset
    ok = sc_game_profile_parse(&profile,
                               "format 2\nprofile p\ncrop off\n"
                               "bind key=a pos=0.5,0.5\n", &line);
    assert(!ok);
    assert(line == 4);

    // bad key
    ok = sc_game_profile_parse(&profile,
                               "format 2\nprofile p\ncrop off\npreset p\n"
                               "bind key=?? pos=0.5,0.5\n",
                               &line);
    assert(!ok);
    assert(line == 5);

    // out-of-range coordinates
    ok = sc_game_profile_parse(&profile,
                               "format 2\nprofile p\ncrop off\npreset p\n"
                               "bind key=a pos=1.5,0.5\n", &line);
    assert(!ok);
}

static void
assert_parse_fails(const char *text, size_t expected_line) {
    struct sc_game_profile profile;
    size_t line = 0;
    assert(!sc_game_profile_parse(&profile, text, &line));
    assert(line == expected_line);
}

static void
test_strict_parse_errors(void) {
    const char *header =
        "format 2\n"
        "profile p\n"
        "hotkey edit=shift+e preset=f8\n"
        "crop off\n"
        "preset p\n";
    char text[512];

#define ASSERT_BAD_LINE(LINE) \
    do { \
        snprintf(text, sizeof(text), "%s%s\n", header, (LINE)); \
        assert_parse_fails(text, 6); \
    } while (0)

    ASSERT_BAD_LINE("aim on sens=1.0oops");
    ASSERT_BAD_LINE("aim on sens=nan");
    ASSERT_BAD_LINE("aim on sens=inf");
    ASSERT_BAD_LINE("humanize on amp=0.003junk");
    ASSERT_BAD_LINE("bind key=a pos=0.5,0.5junk");
    ASSERT_BAD_LINE("bind key=a pos=nan,0.5");
    ASSERT_BAD_LINE("bind key=a pos=0.5,0.5 size=0.0009");
    ASSERT_BAD_LINE("joystick center=0.5,0.5 radius=0.5");
    ASSERT_BAD_LINE("aim start=nan,0.5");
    ASSERT_BAD_LINE("aim start=0.5,0.5 center=0.6,0.6");
    ASSERT_BAD_LINE("aim center=0.5,0.5 radius=0.15");
    ASSERT_BAD_LINE("aim start=0.5,0.5 radius=0.15");
    ASSERT_BAD_LINE("aim radius=0.15 start=0.5,0.5");
    ASSERT_BAD_LINE("aim start=0.5,0.5 start=0.6,0.6");
    ASSERT_BAD_LINE("aim start=1.1,0.5");
    ASSERT_BAD_LINE("swipe key=w from=0.5,0.6 to=0.5,0.4 dur=100ms");
    ASSERT_BAD_LINE("swipe key=w from=0.5,0.6 to=0.5,0.4 every=-1");
    ASSERT_BAD_LINE("swipe key=w from=0.5,0.6 to=0.5,0.4 dur=5001");

#undef ASSERT_BAD_LINE

    assert_parse_fails("format 2\nprofile ../p\n"
                       "hotkey edit=shift+e preset=f8\ncrop off\n"
                       "preset p\n", 2);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\n"
                       "crop off\npreset two words\n", 5);
    assert_parse_fails("format 2\nprofile p\nprofile q\n"
                       "hotkey edit=shift+e preset=f8\ncrop off\n"
                       "preset p\n", 3);

    // Error lines are physical source lines, including blank lines.
    assert_parse_fails("format 2\nprofile p\n\n"
                       "hotkey edit=shift+e preset=f8\n"
                       "crop off\npreset p\n\n"
                       "bind key=a pos=0.5,0.5 size=nan\n", 8);

    // The schema marker is mandatory, exact, current and appears once at the
    // first effective line (comments and blank lines do not count).
    assert_parse_fails("profile p\nhotkey edit=shift+e preset=f8\n"
                       "preset p\n", 1);
    assert_parse_fails("format 1\nprofile p\n", 1);
    assert_parse_fails("format 01\nprofile p\n", 1);
    assert_parse_fails("format 2 extra\nprofile p\n", 1);
    assert_parse_fails("format 2\nformat 2\nprofile p\n", 2);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\ncrop off\n"
                       "preset p\nbind key=- pos=0.5,0.5\n", 6);

    // Crop is a required, unique global field and must precede all presets.
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\npreset p\n", 4);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\ncrop off\n"
                       "crop off\npreset p\n", 5);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\npreset p\n"
                       "crop off\n", 5);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\n"
                       "crop off extra\npreset p\n", 4);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\n"
                       "crop rect=0,0,1\npreset p\n", 4);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\n"
                       "crop rect=nan,0,1,1\npreset p\n", 4);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\n"
                       "crop rect=-0.1,0,1,1\npreset p\n", 4);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\n"
                       "crop rect=0.8,0,0.2,1\npreset p\n", 4);
    assert_parse_fails("format 2\nprofile p\n"
                       "hotkey edit=shift+e preset=f8\n"
                       "crop rect=0,0,0.009,1\npreset p\n", 4);

    const char *commented =
        "# current game profile schema\n"
        "\n"
        "format 2\n"
        "profile p\n"
        "hotkey edit=shift+e preset=f8\n"
        "crop off\n"
        "preset p\n";
    struct sc_game_profile profile;
    size_t line = 0;
    assert(sc_game_profile_parse(&profile, commented, &line));
}

int
main(int argc, char **argv) {
    (void) argc;
    (void) argv;
    test_parse_default();
    test_name_validation();
    test_model_validation();
    test_unbound_keys_roundtrip();
    test_parse_serialize_roundtrip();
    test_parse_errors();
    test_strict_parse_errors();
    return 0;
}
