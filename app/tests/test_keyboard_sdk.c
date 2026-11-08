#include "common.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "control_msg.h"
#include "input_events.h"
#include "keyboard_sdk.h"

#define MAX_RECORDED_TEXTS 16

static char *recorded_texts[MAX_RECORDED_TEXTS];
static size_t recorded_text_count;

// keyboard_sdk.c only needs the controller queue boundary. Recording and
// taking ownership here keeps the test independent from sockets and threads.
bool
sc_controller_push_msg(struct sc_controller *controller,
                       const struct sc_control_msg *msg) {
    (void) controller;
    assert(msg->type == SC_CONTROL_MSG_TYPE_INJECT_TEXT);
    assert(recorded_text_count < ARRAY_LEN(recorded_texts));
    recorded_texts[recorded_text_count++] = msg->inject_text.text;
    return true;
}

static void
reset_recording(void) {
    for (size_t i = 0; i < recorded_text_count; ++i) {
        free(recorded_texts[i]);
        recorded_texts[i] = NULL;
    }
    recorded_text_count = 0;
}

static void
process_text(enum sc_key_inject_mode mode, const char *text) {
    struct sc_controller controller = {0};
    struct sc_keyboard_sdk keyboard;
    sc_keyboard_sdk_init(&keyboard, &controller, mode, false);
    const struct sc_text_event event = {.text = text};
    keyboard.key_processor.ops->process_text(&keyboard.key_processor, &event);
}

static void
assert_no_text(enum sc_key_inject_mode mode, const char *input) {
    reset_recording();
    process_text(mode, input);
    assert(recorded_text_count == 0);
}

static void
assert_text(enum sc_key_inject_mode mode, const char *input,
            const char *expected) {
    reset_recording();
    process_text(mode, input);
    assert(recorded_text_count == 1);
    assert(!strcmp(recorded_texts[0], expected));
}

static void
test_mixed_filters_ascii_letters_and_spaces(void) {
    assert_no_text(SC_KEY_INJECT_MODE_MIXED, "a");
    assert_no_text(SC_KEY_INJECT_MODE_MIXED, "W");
    assert_no_text(SC_KEY_INJECT_MODE_MIXED, "www");
    assert_no_text(SC_KEY_INJECT_MODE_MIXED, "a b Z");
    assert_no_text(SC_KEY_INJECT_MODE_MIXED, "   ");
}

static void
test_mixed_forwards_utf8_and_non_letter_ascii(void) {
    assert_text(SC_KEY_INJECT_MODE_MIXED, "中文输入", "中文输入");
    assert_text(SC_KEY_INJECT_MODE_MIXED, "a!b,c?", "!,?");
    assert_text(SC_KEY_INJECT_MODE_MIXED, "1 + 2", "1+2");
}

static void
test_raw_drops_text(void) {
    assert_no_text(SC_KEY_INJECT_MODE_RAW, "www");
    assert_no_text(SC_KEY_INJECT_MODE_RAW, "中文 ! 123");
}

static void
test_text_forwards_complete_input(void) {
    assert_text(SC_KEY_INJECT_MODE_TEXT, "www 中文 ! 123",
                "www 中文 ! 123");
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_mixed_filters_ascii_letters_and_spaces();
    test_mixed_forwards_utf8_and_non_letter_ascii();
    test_raw_drops_text();
    test_text_forwards_complete_input();
    reset_recording();
    return 0;
}
