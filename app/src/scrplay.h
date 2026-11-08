#ifndef SCRPLAY_H
#define SCRPLAY_H

#include "common.h"

#include "options.h"

enum scrplay_exit_code {
    // Normal program termination
    SCRPLAY_EXIT_SUCCESS,

    // No connection could be established
    SCRPLAY_EXIT_FAILURE,

    // Device was disconnected while running
    SCRPLAY_EXIT_DISCONNECTED,
};

enum scrplay_exit_code
scrplay(struct scrplay_options *options);

#endif
