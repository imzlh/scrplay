#include "common.h"

#include <stdbool.h>
#include <stdio.h>
#ifdef HAVE_V4L2
# include <libavdevice/avdevice.h>
#endif
#define SDL_MAIN_HANDLED // avoid link error on Linux Windows Subsystem
#include <SDL2/SDL.h>

#include "cli.h"
#include "options.h"
#include "scrplay.h"
#include "usb/scrplay_otg.h"
#include "util/log.h"
#include "util/net.h"
#include "util/thread.h"
#include "version.h"

#ifdef _WIN32
#include <windows.h>
#include "util/str.h"
#endif

static int
main_scrplay(int argc, char *argv[]) {
#ifdef _WIN32
    // disable buffering, we want logs immediately
    // even line buffering (setvbuf() with mode _IOLBF) is not sufficient
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
#endif

    printf("scrplay " SCRPLAY_VERSION
           " <https://github.com/imzlh/scrplay>\n");

    struct scrplay_cli_args args = {
        .opts = scrplay_options_default,
        .help = false,
        .version = false,
        .pause_on_exit = SC_PAUSE_ON_EXIT_FALSE,
    };

#ifndef NDEBUG
    args.opts.log_level = SC_LOG_LEVEL_DEBUG;
#endif

    enum scrplay_exit_code ret;

    if (!scrplay_parse_args(&args, argc, argv)) {
        ret = SCRPLAY_EXIT_FAILURE;
        goto end;
    }

    sc_set_log_level(args.opts.log_level);

    if (args.help) {
        scrplay_print_usage(argv[0]);
        ret = SCRPLAY_EXIT_SUCCESS;
        goto end;
    }

    if (args.version) {
        scrplay_print_version();
        ret = SCRPLAY_EXIT_SUCCESS;
        goto end;
    }

    // The current thread is the main thread
    SC_MAIN_THREAD_ID = sc_thread_get_id();

#ifdef SCRPLAY_LAVF_REQUIRES_REGISTER_ALL
    av_register_all();
#endif

#ifdef HAVE_V4L2
    if (args.opts.v4l2_device) {
        avdevice_register_all();
    }
#endif

    if (!net_init()) {
        ret = SCRPLAY_EXIT_FAILURE;
        goto end;
    }

    sc_log_configure();

#ifdef HAVE_USB
    ret = args.opts.otg ? scrplay_otg(&args.opts) : scrplay(&args.opts);
#else
    ret = scrplay(&args.opts);
#endif

end:
    if (args.pause_on_exit == SC_PAUSE_ON_EXIT_TRUE ||
            (args.pause_on_exit == SC_PAUSE_ON_EXIT_IF_ERROR &&
                ret != SCRPLAY_EXIT_SUCCESS)) {
        printf("Press Enter to continue...\n");
        getchar();
    }

    return ret;
}

int
main(int argc, char *argv[]) {
#ifndef _WIN32
    return main_scrplay(argc, argv);
#else
    (void) argc;
    (void) argv;
    int wargc;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv) {
        LOG_OOM();
        return SCRPLAY_EXIT_FAILURE;
    }

    char **argv_utf8 = malloc((wargc + 1) * sizeof(*argv_utf8));
    if (!argv_utf8) {
        LOG_OOM();
        LocalFree(wargv);
        return SCRPLAY_EXIT_FAILURE;
    }

    argv_utf8[wargc] = NULL;

    for (int i = 0; i < wargc; ++i) {
        argv_utf8[i] = sc_str_from_wchars(wargv[i]);
        if (!argv_utf8[i]) {
            LOG_OOM();
            for (int j = 0; j < i; ++j) {
                free(argv_utf8[j]);
            }
            LocalFree(wargv);
            free(argv_utf8);
            return SCRPLAY_EXIT_FAILURE;
        }
    }

    LocalFree(wargv);

    int ret = main_scrplay(wargc, argv_utf8);

    for (int i = 0; i < wargc; ++i) {
        free(argv_utf8[i]);
    }
    free(argv_utf8);

    return ret;
#endif
}
