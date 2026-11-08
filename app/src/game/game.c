#include "game.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#ifndef _WIN32
# include <dirent.h>
# include <errno.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <unistd.h>
#endif

#include "android/keycodes.h"
#include "cimgui.h"
#include "display.h"
#include "events.h"
#include "game_storage.h"
#include "input_events.h"
#include "mouse_capture.h"
#include "screen.h"
#include "util/log.h"
#include "util/str.h"

/** Downcast key processor to sc_game */
#define KP_DOWNCAST(KP) container_of(KP, struct sc_game, kp_trait)
/** Downcast mouse processor to sc_game */
#define MP_DOWNCAST(MP) container_of(MP, struct sc_game, mp_trait)

#define SC_GAME_AUTOSAVE_DELAY SC_TICK_FROM_MS(700)
#define SC_GAME_NATIVE_HOLD_DELAY SC_TICK_FROM_MS(600)

static uint8_t
game_mods_from_event(uint16_t mods_state) {
    uint8_t mods = 0;
    if (mods_state & KMOD_SHIFT) {
        mods |= SC_GAME_MOD_SHIFT;
    }
    if (mods_state & KMOD_CTRL) {
        mods |= SC_GAME_MOD_CTRL;
    }
    if (mods_state & KMOD_ALT) {
        mods |= SC_GAME_MOD_ALT;
    }
    return mods;
}

static bool
hotkey_matches(const struct sc_game_hotkey *hotkey, SDL_Keycode keycode,
               uint16_t mods_state) {
    return hotkey->key.type == SC_GAME_KEY_KEYBOARD
        && hotkey->key.value == (uint32_t) keycode
        && (bool) (mods_state & KMOD_SHIFT) == hotkey->shift
        && (bool) (mods_state & KMOD_CTRL) == hotkey->ctrl
        && (bool) (mods_state & KMOD_ALT) == hotkey->alt;
}

static bool
mapped_key_held(const struct sc_game *game, uint32_t keycode) {
    for (size_t i = 0; i < game->mapped_key_count; ++i) {
        if (game->mapped_keys[i] == keycode) {
            return true;
        }
    }
    return false;
}

static void
mapped_key_add(struct sc_game *game, uint32_t keycode) {
    if (!mapped_key_held(game, keycode)
            && game->mapped_key_count < SC_GAME_MAX_PRESSED_KEYS) {
        game->mapped_keys[game->mapped_key_count++] = keycode;
    }
}

static bool
mapped_key_remove(struct sc_game *game, uint32_t keycode) {
    for (size_t i = 0; i < game->mapped_key_count; ++i) {
        if (game->mapped_keys[i] == keycode) {
            game->mapped_keys[i] =
                game->mapped_keys[--game->mapped_key_count];
            return true;
        }
    }
    return false;
}

static bool
printable_mapped_key_held(const struct sc_game *game) {
    for (size_t i = 0; i < game->mapped_key_count; ++i) {
        if (game->mapped_keys[i] >= 0x20 && game->mapped_keys[i] < 0x7f) {
            return true;
        }
    }
    return false;
}

static void
activate_short_preset_hotkey(struct sc_game *game) {
    if (!game->profile.preset_count) {
        return;
    }

    size_t preset = game->profile.current_preset;
    if (game->runtime == SC_GAME_RUNTIME_NATIVE) {
        // Native mode retains the last mapped preset, so a short press returns
        // exactly where the player came from.
        if (preset >= game->profile.preset_count) {
            preset = 0;
        }
    } else {
        // Short presses only rotate gameplay presets. Native mode is reserved
        // for an intentional hold, so combat cannot enter it by accident.
        preset = (preset + 1) % game->profile.preset_count;
    }
    sc_game_select_preset(game, preset);
}

static void
update_preset_hotkey_hold(struct sc_game *game) {
    if (!game->preset_hotkey_held || game->preset_hotkey_long_triggered
            || sc_tick_now() < game->preset_hotkey_deadline) {
        return;
    }

    game->preset_hotkey_long_triggered = true;
    if (game->runtime != SC_GAME_RUNTIME_NATIVE) {
        sc_game_select_native(game);
    }
}

static void
key_processor_process_key(struct sc_key_processor *kp,
                          const struct sc_key_event *event,
                          uint64_t ack_to_wait) {
    struct sc_game *game = KP_DOWNCAST(kp);

    if (game->mode == SC_GAME_MODE_EDIT) {
        // in edit mode, keys are handled by the editor (via the SDL event
        // pre-hook), never forwarded to the device
        return;
    }

    if (game->runtime == SC_GAME_RUNTIME_NATIVE) {
        if (game->real_kp) {
            game->real_kp->ops->process_key(game->real_kp, event,
                                            ack_to_wait);
        }
        return;
    }

    bool down = event->action == SC_ACTION_DOWN;
    if (down && event->repeat) {
        // The original DOWN already established ownership. Reprocessing a
        // repeat after a required modifier was released/re-pressed could
        // otherwise revive an intentionally cancelled combo press.
        return;
    }
    // Synchronize combo modifiers before asking the current preset to claim
    // the key. Ctrl remains a normal device key; Alt is the mapped-runtime
    // mouse-release modifier and is filtered below.
    sc_game_engine_set_mods(&game->engine,
                            game_mods_from_event(event->mods_state));

    if (event->keycode == sc_keycode_from_sdl(SDLK_LALT)
            || event->keycode == sc_keycode_from_sdl(SDLK_RALT)) {
        return;
    }
    bool was_mapped = mapped_key_held(game, event->keycode);
    bool used = sc_game_engine_on_key(&game->engine, event->keycode, down);
    if (down && used) {
        mapped_key_add(game, event->keycode);
    } else if (!down) {
        was_mapped = mapped_key_remove(game, event->keycode) || was_mapped;
    }
    if (used || was_mapped) {
        // consumed by the game mapping
        return;
    }

    if (game->real_kp) {
        struct sc_key_event forwarded = *event;
        // Alt is a local mouse-release chord while mapped. Ctrl and its
        // metastate deliberately remain intact for ordinary Android combos.
        forwarded.mods_state &= ~KMOD_ALT;
        game->real_kp->ops->process_key(game->real_kp, &forwarded,
                                        ack_to_wait);
    }
}

static void
key_processor_process_text(struct sc_key_processor *kp,
                           const struct sc_text_event *event) {
    struct sc_game *game = KP_DOWNCAST(kp);

    if (game->mode == SC_GAME_MODE_EDIT) {
        return;
    }
    if (game->real_kp && game->real_kp->ops->process_text) {
        game->real_kp->ops->process_text(game->real_kp, event);
    }
}

static const struct sc_key_processor_ops key_processor_ops = {
    .process_key = key_processor_process_key,
    .process_text = key_processor_process_text,
};

// Transform relative mouse motion (window points) to frame-space motion,
// taking the display orientation into account.
static void
aim_delta_to_frame(struct sc_game *game, float dx, float dy, float *fdx,
                   float *fdy) {
    struct sc_screen_fpoint frame;
    bool ok = sc_screen_window_to_frame_vector_f(
        game->screen, (struct sc_screen_fpoint) {.x = dx, .y = dy}, &frame);
    if (!ok) {
        *fdx = 0;
        *fdy = 0;
        return;
    }
    *fdx = frame.x;
    *fdy = frame.y;
}

static void
mouse_processor_process_mouse_motion(struct sc_mouse_processor *mp,
                                 const struct sc_mouse_motion_event *event) {
    struct sc_game *game = MP_DOWNCAST(mp);

    if (game->mode == SC_GAME_MODE_EDIT) {
        return;
    }
    if (game->runtime == SC_GAME_RUNTIME_NATIVE) {
        if (game->real_mp) {
            game->real_mp->ops->process_mouse_motion(game->real_mp, event);
        }
        return;
    }
    if (!sc_game_is_captured(game)) {
        return; // aim only while the mouse is captured
    }
    float fdx;
    float fdy;
    aim_delta_to_frame(game, event->xrel, event->yrel, &fdx, &fdy);
    sc_game_engine_add_aim_delta(&game->engine, fdx, fdy);
    // Sample first, then re-anchor. The warp is configured to emit no motion,
    // so it cannot feed an equal-and-opposite delta back into camera aim.
    sc_mouse_capture_recenter(&game->screen->mc);
}

static void
mouse_processor_process_mouse_click(struct sc_mouse_processor *mp,
                                    const struct sc_mouse_click_event *event) {
    struct sc_game *game = MP_DOWNCAST(mp);

    if (game->mode == SC_GAME_MODE_EDIT) {
        return;
    }
    if (game->runtime == SC_GAME_RUNTIME_NATIVE) {
        if (game->real_mp) {
            game->real_mp->ops->process_mouse_click(game->real_mp, event);
        }
        return;
    }
    bool down = event->action == SC_ACTION_DOWN;
    if (sc_game_engine_on_mouse_button(&game->engine, event->button, down)) {
        return; // consumed by the game mapping
    }
    // in captured mode, unmapped clicks are dropped (the mouse belongs to
    // the game); when released, the mouse capture handler re-captures
}

static void
mouse_processor_process_mouse_scroll(struct sc_mouse_processor *mp,
                                     const struct sc_mouse_scroll_event *event) {
    struct sc_game *game = MP_DOWNCAST(mp);
    if (game->mode != SC_GAME_MODE_EDIT
            && game->runtime == SC_GAME_RUNTIME_NATIVE && game->real_mp
            && game->real_mp->ops->process_mouse_scroll) {
        game->real_mp->ops->process_mouse_scroll(game->real_mp, event);
    }
}

static void
mouse_processor_process_touch(struct sc_mouse_processor *mp,
                              const struct sc_touch_event *event) {
    struct sc_game *game = MP_DOWNCAST(mp);
    if (game->mode != SC_GAME_MODE_EDIT
            && game->runtime == SC_GAME_RUNTIME_NATIVE && game->real_mp
            && game->real_mp->ops->process_touch) {
        game->real_mp->ops->process_touch(game->real_mp, event);
    }
}

static const struct sc_mouse_processor_ops mouse_processor_ops = {
    .process_mouse_motion = mouse_processor_process_mouse_motion,
    .process_mouse_click = mouse_processor_process_mouse_click,
    .process_mouse_scroll = mouse_processor_process_mouse_scroll,
    .process_touch = mouse_processor_process_touch,
};

// ---- paths -----------------------------------------------------------------

static bool
build_config_paths(struct sc_game *game) {
#ifdef _WIN32
    // SDL returns a newly allocated UTF-8 path with a trailing separator and
    // handles roaming/local application-data discovery for native Windows.
    char *pref = SDL_GetPrefPath("scrplay", "scrplay");
    if (!pref) {
        LOGE("Game mode: could not locate the Windows config directory: %s",
             SDL_GetError());
        return false;
    }
    int profiles_written =
        snprintf(game->profiles_dir, sizeof(game->profiles_dir),
                 "%sgameprofiles", pref);
    int ini_written = snprintf(game->ini_path, sizeof(game->ini_path),
                               "%sgameui.ini", pref);
    SDL_free(pref);
    if (profiles_written < 0
            || (size_t) profiles_written >= sizeof(game->profiles_dir)
            || ini_written < 0
            || (size_t) ini_written >= sizeof(game->ini_path)) {
        LOGE("Game mode: Windows config path is too long");
        return false;
    }
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) {
        // XDG_CONFIG_HOME already denotes the config directory; unlike HOME,
        // it must not receive another ".config" component.
        snprintf(game->profiles_dir, sizeof(game->profiles_dir),
                 "%.*s/scrplay/gameprofiles",
                 (int) sizeof(game->profiles_dir) - 22, xdg);
        snprintf(game->ini_path, sizeof(game->ini_path),
                 "%.*s/scrplay/gameui.ini",
                 (int) sizeof(game->ini_path) - 20, xdg);
    } else if (home && *home) {
        snprintf(game->profiles_dir, sizeof(game->profiles_dir),
                 "%.*s/.config/scrplay/gameprofiles",
                 (int) sizeof(game->profiles_dir) - 30, home);
        snprintf(game->ini_path, sizeof(game->ini_path),
                 "%.*s/.config/scrplay/gameui.ini",
                 (int) sizeof(game->ini_path) - 28, home);
    } else {
        snprintf(game->profiles_dir, sizeof(game->profiles_dir),
                 ".config/scrplay/gameprofiles");
        snprintf(game->ini_path, sizeof(game->ini_path),
                 ".config/scrplay/gameui.ini");
    }
#endif
    return true;
}

#ifndef _WIN32

// ---- legacy config dir migration -------------------------------------------

// The legacy config directory keeps its pre-rename product name: it is
// only read (never modified) to migrate config data to the new location.
#define SC_GAME_LEGACY_CONFIG_DIR "scrcpy"
#define SC_GAME_CONFIG_DIR "scrplay"

static bool
path_join(char *out, size_t size, const char *dir, const char *name) {
    int written = snprintf(out, size, "%s/%s", dir, name);
    return written >= 0 && (size_t) written < size;
}

static bool
copy_file(const char *src, const char *dst) {
    FILE *fp_src = fopen(src, "rb");
    if (!fp_src) {
        return false;
    }
    FILE *fp_dst = fopen(dst, "wb"); // truncated copy, like cp(1)
    if (!fp_dst) {
        fclose(fp_src);
        return false;
    }
    char buf[4096];
    bool ok = true;
    size_t n;
    while (ok && (n = fread(buf, 1, sizeof(buf), fp_src)) > 0) {
        ok = fwrite(buf, 1, n, fp_dst) == n;
    }
    ok = !ferror(fp_src) && ok;
    if (fclose(fp_dst) != 0) {
        ok = false;
    }
    fclose(fp_src);
    return ok;
}

// Best-effort recursive copy. The source directory is only read, never
// modified or removed. Symlinks and other special entry types are skipped
// (never followed).
static bool
copy_tree(const char *src_dir, const char *dst_dir) {
    if (mkdir(dst_dir, 0755) && errno != EEXIST) {
        return false;
    }
    DIR *dir = opendir(src_dir);
    if (!dir) {
        return false;
    }
    bool ok = true;
    struct dirent *entry;
    while (ok && (entry = readdir(dir))) {
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) {
            continue;
        }
        char src_path[SC_GAME_PROFILES_DIR_LEN];
        char dst_path[SC_GAME_PROFILES_DIR_LEN];
        if (!path_join(src_path, sizeof(src_path), src_dir, name)
                || !path_join(dst_path, sizeof(dst_path), dst_dir, name)) {
            ok = false;
            break;
        }
        struct stat st;
        if (lstat(src_path, &st)) {
            ok = false;
            break;
        }
        if (S_ISDIR(st.st_mode)) {
            ok = copy_tree(src_path, dst_path);
        } else if (S_ISREG(st.st_mode)) {
            ok = copy_file(src_path, dst_path);
        }
    }
    closedir(dir);
    return ok;
}

// Best-effort recursive removal, used only to clean up a partially written
// destination tree so the migration can be retried on the next run.
static bool
remove_tree(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) {
        return unlink(path) == 0;
    }
    bool ok = true;
    struct dirent *entry;
    while (ok && (entry = readdir(dir))) {
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) {
            continue;
        }
        char child[SC_GAME_PROFILES_DIR_LEN];
        if (!path_join(child, sizeof(child), path, name)) {
            ok = false;
            break;
        }
        struct stat st;
        if (lstat(child, &st)) {
            ok = false;
            break;
        }
        ok = S_ISDIR(st.st_mode) ? remove_tree(child)
                                 : unlink(child) == 0;
    }
    closedir(dir);
    return ok ? rmdir(path) == 0 : false;
}

// One-time migration of the config directory to its renamed location. It
// runs only when the destination does not exist yet and the legacy one does;
// the legacy directory is never modified nor removed. Failures are logged
// but not fatal: the new location is used regardless (game mode then starts
// from fresh defaults there).
static void
sc_game_migrate_config_dir(void) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    char old_dir[SC_GAME_PROFILES_DIR_LEN];
    char new_dir[SC_GAME_PROFILES_DIR_LEN];
    int old_written;
    int new_written;
    if (xdg && *xdg) {
        // mirror build_config_paths(): XDG_CONFIG_HOME already denotes the
        // config directory; unlike HOME it takes no ".config" component
        old_written = snprintf(old_dir, sizeof(old_dir),
                               "%.*s/" SC_GAME_LEGACY_CONFIG_DIR,
                               (int) sizeof(old_dir)
                                   - (int) sizeof("/" SC_GAME_LEGACY_CONFIG_DIR),
                               xdg);
        new_written = snprintf(new_dir, sizeof(new_dir),
                               "%.*s/" SC_GAME_CONFIG_DIR,
                               (int) sizeof(new_dir)
                                   - (int) sizeof("/" SC_GAME_CONFIG_DIR),
                               xdg);
    } else if (home && *home) {
        old_written = snprintf(old_dir, sizeof(old_dir),
                               "%.*s/.config/" SC_GAME_LEGACY_CONFIG_DIR,
                               (int) sizeof(old_dir)
                                   - (int) sizeof("/.config/"
                                                  SC_GAME_LEGACY_CONFIG_DIR),
                               home);
        new_written = snprintf(new_dir, sizeof(new_dir),
                               "%.*s/.config/" SC_GAME_CONFIG_DIR,
                               (int) sizeof(new_dir)
                                   - (int) sizeof("/.config/"
                                                  SC_GAME_CONFIG_DIR),
                               home);
    } else {
        return; // no config base directory, nothing to migrate
    }
    if (old_written < 0 || (size_t) old_written >= sizeof(old_dir)
            || new_written < 0
            || (size_t) new_written >= sizeof(new_dir)) {
        LOGW("Game mode: config path is too long, skipping migration");
        return;
    }

    struct stat st;
    if (!stat(new_dir, &st) || stat(old_dir, &st)) {
        // already using the new directory, or nothing to migrate from
        return;
    }

    if (copy_tree(old_dir, new_dir)) {
        LOGI("Migrated config directory '%s' to '%s'", old_dir, new_dir);
    } else {
        LOGW("Game mode: could not migrate config directory '%s' to '%s'",
             old_dir, new_dir);
        // Roll back the partial destination tree (it did not exist before
        // this migration started, so it only holds what we just copied) so
        // the next run can retry. The legacy directory stays untouched.
        remove_tree(new_dir);
    }
}

#endif // !defined(_WIN32)

// Best-effort detection of the desktop HiDPI scale factor (KDE Plasma
// stores per-output scale in kwinoutputconfig.json; Wayland does not expose
// it to SDL in a portable way).
static float
detect_desktop_scale(void) {
    const char *env = getenv("QT_SCALE_FACTOR");
    if (env && *env) {
        float f = strtof(env, NULL);
        if (f >= 0.5f && f <= 4.0f) {
            return f;
        }
    }
    env = getenv("GDK_SCALE");
    if (env && *env) {
        float f = strtof(env, NULL);
        if (f >= 0.5f && f <= 4.0f) {
            return f;
        }
    }
    const char *home = getenv("HOME");
    if (home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.config/kwinoutputconfig.json",
                 home);
        FILE *fp = fopen(path, "r");
        if (fp) {
            // crude scan for the largest "scale":<float> entry
            char buf[4096];
            float best = 1.0f;
            size_t len = 0;
            while (fgets(buf, sizeof(buf), fp)) {
                len += strlen(buf);
                char *p = buf;
                while ((p = strstr(p, "\"scale\"")) != NULL) {
                    p += strlen("\"scale\"");
                    while (*p == ' ' || *p == ':' || *p == '\t') {
                        ++p;
                    }
                    float f = strtof(p, NULL);
                    if (f >= 0.5f && f <= 4.0f && f > best) {
                        best = f;
                    }
                }
                if (len > 256 * 1024) {
                    break; // safety bound
                }
            }
            fclose(fp);
            if (best > 1.0f) {
                return best;
            }
        }
    }
    return 1.0f;
}

// ---- profile files ---------------------------------------------------------

bool
sc_game_save_profile(struct sc_game *game) {
    game->save_status = SC_GAME_SAVE_STATUS_SAVING;
    enum sc_game_storage_status status =
        sc_game_storage_save(game->profiles_dir, &game->profile);
    if (status != SC_GAME_STORAGE_OK) {
        LOGE("Game mode: could not save profile '%s': %s",
             game->profile.name,
             sc_game_storage_status_to_string(status));
        game->save_status = SC_GAME_SAVE_STATUS_ERROR;
        return false;
    }
    game->profile_dirty = false;
    game->autosave_deadline = 0;
    game->save_status = SC_GAME_SAVE_STATUS_SAVED;
    LOGI("Game mode: profile '%s' saved", game->profile.name);
    return true;
}

void
sc_game_mark_profile_dirty(struct sc_game *game) {
    game->profile_dirty = true;
    game->save_status = SC_GAME_SAVE_STATUS_SAVING;
    game->autosave_deadline = sc_tick_now() + SC_GAME_AUTOSAVE_DELAY;
}

enum sc_game_save_status
sc_game_get_save_status(const struct sc_game *game) {
    return game->save_status;
}

static void
autosave_profile_if_due(struct sc_game *game) {
    if (game->profile_dirty && game->autosave_deadline
            && sc_tick_now() >= game->autosave_deadline) {
        sc_game_save_profile(game);
    }
}

bool
sc_game_load_profile(struct sc_game *game, const char *name) {
    // Validate the target before flushing the current working copy, matching
    // the existing switch semantics: a malformed request is not a switch.
    if (!sc_game_profile_name_is_valid(name)) {
        LOGE("Game mode: invalid profile name");
        return false;
    }
    // Never discard pending edits when switching profiles. This is done
    // before opening the target so reloading the current profile also reads
    // the just-saved contents rather than a stale snapshot.
    if (game->profile_dirty && !sc_game_save_profile(game)) {
        LOGE("Game mode: refusing to load '%s' because the current profile "
             "could not be saved", name);
        return false;
    }
    struct sc_game_profile profile;
    size_t error_line = 0;
    enum sc_game_storage_status status =
        sc_game_storage_load(game->profiles_dir, name, &profile, &error_line);
    if (status != SC_GAME_STORAGE_OK) {
        if (status == SC_GAME_STORAGE_NOT_FOUND) {
            LOGW("Game mode: profile '%s' not found", name);
        } else if (status == SC_GAME_STORAGE_INVALID_PROFILE && error_line) {
            LOGE("Game mode: invalid profile '%s' at line %zu", name,
                 error_line);
        } else {
            LOGE("Game mode: could not load profile '%s': %s", name,
                 sc_game_storage_status_to_string(status));
        }
        return false;
    }

    bool opened_from_picker = game->ui.inited && game->ui.picker_open;
    game->profile = profile;
    ++game->profile_revision;
    game->profile_loaded = true;
    game->profile_dirty = false;
    game->autosave_deadline = 0;
    game->save_status = SC_GAME_SAVE_STATUS_SAVED;
    sc_game_cancel_binding(game);
    sc_game_apply_profile(game);
    if (opened_from_picker) {
        game->ui.picker_open = false;
    }
    LOGI("Game mode: profile '%s' loaded (%zu presets)", name,
         profile.preset_count);
    return true;
}

bool
sc_game_create_profile(struct sc_game *game, const char *name) {
    if (!sc_game_profile_name_is_valid(name)) {
        LOGE("Game mode: invalid profile name");
        return false;
    }
    struct sc_game_profile existing;
    enum sc_game_storage_status existing_status =
        sc_game_storage_load(game->profiles_dir, name, &existing, NULL);
    if (existing_status != SC_GAME_STORAGE_NOT_FOUND) {
        if (existing_status == SC_GAME_STORAGE_OK) {
            LOGW("Game mode: profile '%s' already exists", name);
        } else {
            // A file with this destination may be damaged or temporarily
            // unreadable. Never replace it just because it could not be
            // parsed: creation is expected to be non-destructive.
            LOGE("Game mode: refusing to replace profile '%s': %s", name,
                 sc_game_storage_status_to_string(existing_status));
        }
        return false;
    }
    if (game->profile_dirty && !sc_game_save_profile(game)) {
        LOGE("Game mode: refusing to create '%s' because the current profile "
             "could not be saved", name);
        return false;
    }
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, name);
    // stage as working copy then save
    struct sc_game_profile saved = game->profile;
    bool was_loaded = game->profile_loaded;
    game->profile = profile;
    bool ok = sc_game_save_profile(game);
    if (ok) {
        ++game->profile_revision;
        sc_game_cancel_binding(game);
        sc_game_apply_profile(game);
        game->profile_loaded = true;
        if (game->ui.inited && game->ui.picker_open) {
            game->ui.picker_open = false;
        }
    } else {
        game->profile = saved;
        game->profile_loaded = was_loaded;
    }
    return ok;
}

bool
sc_game_profile_exists(struct sc_game *game, const char *name) {
    if (!sc_game_profile_name_is_valid(name)) {
        return false;
    }
    struct sc_game_profile profile;
    return sc_game_storage_load(game->profiles_dir, name, &profile, NULL)
        == SC_GAME_STORAGE_OK;
}

void
sc_game_delete_profile(struct sc_game *game, const char *name) {
    enum sc_game_storage_status status =
        sc_game_storage_delete(game->profiles_dir, name);
    if (status != SC_GAME_STORAGE_OK && status != SC_GAME_STORAGE_NOT_FOUND) {
        LOGW("Game mode: could not delete profile '%s': %s", name,
             sc_game_storage_status_to_string(status));
    }
}

size_t
sc_game_list_profiles(struct sc_game *game,
                      char names[][SC_GAME_PROFILE_NAME_MAX], size_t max) {
    return sc_game_storage_list(game->profiles_dir, names, max);
}

// ---- mode / capture --------------------------------------------------------

static bool
runtime_can_map(const struct sc_game *game) {
    return game->runtime == SC_GAME_RUNTIME_MAPPED
        && game->mode == SC_GAME_MODE_PLAY && !game->picker_blocking;
}

static void
sync_text_input(struct sc_game *game) {
    bool focused = game->screen
                && (SDL_GetWindowFlags(game->screen->window)
                    & SDL_WINDOW_INPUT_FOCUS);
    bool native_play = game->runtime == SC_GAME_RUNTIME_NATIVE
                    && game->mode == SC_GAME_MODE_PLAY
                    && !game->picker_blocking;
    bool editor_text = game->ui.inited
                    && (game->mode == SC_GAME_MODE_EDIT
                        || game->ui.picker_open)
                    && igGetIO_Nil()->WantTextInput;
    bool want = focused && (native_play || editor_text);
    bool active = SDL_IsTextInputActive() == SDL_TRUE;
    if (want == active) {
        return;
    }
    if (want) {
        SDL_StartTextInput();
    } else {
#if SDL_VERSION_ATLEAST(2, 0, 22)
        SDL_ClearComposition();
#endif
        SDL_StopTextInput();
    }
}

static void
release_direct_touch(struct sc_game *game) {
    sc_game_direct_touch_release(&game->direct_touch, game->controller);
}

static void
recapture_after_direct_touch(struct sc_game *game) {
    if (runtime_can_map(game) && !game->alt_pressed
            && !game->direct_touch.active
            && !game->direct_touch.awaiting_button_up
            && !sc_game_is_captured(game)) {
        ImGuiIO *io = game->ui.inited ? igGetIO_Nil() : NULL;
        if (!io || !io->WantCaptureMouse) {
            sc_game_request_capture(game, true);
        }
    }
}

static void
apply_runtime_state(struct sc_game *game) {
    bool mapped = runtime_can_map(game);

    release_direct_touch(game);
    game->alt_pressed = 0;
    game->mapped_key_count = 0;
    sc_game_engine_release_all(&game->engine);
    sc_game_engine_set_mods(&game->engine, 0);
    sc_game_engine_set_active(&game->engine, mapped);

    game->mp_trait.relative_mode = mapped
        || (game->runtime == SC_GAME_RUNTIME_NATIVE && game->real_mp
            && game->real_mp->relative_mode);

    if (game->screen) {
        sc_input_manager_release_all(&game->screen->im);
        sc_input_manager_set_game_mode(&game->screen->im,
                                       game->runtime
                                           == SC_GAME_RUNTIME_MAPPED);
        sc_mouse_capture_set_recenter(&game->screen->mc, mapped);
        sc_mouse_capture_set_keys(&game->screen->mc,
            game->runtime == SC_GAME_RUNTIME_NATIVE
                ? game->native_mouse_capture_keys : 0);
        sc_game_request_capture(game, false);
        // SDL2 enables text input (and therefore the desktop IME) globally on
        // desktop startup. Mapped play must explicitly turn it off so a lone
        // Shift remains a gameplay key instead of toggling Chinese input.
        sync_text_input(game);
    }
}

bool
sc_game_is_native(const struct sc_game *game) {
    return game->runtime == SC_GAME_RUNTIME_NATIVE;
}

bool
sc_game_select_preset(struct sc_game *game, size_t preset) {
    if (preset >= game->profile.preset_count) {
        return false;
    }
    game->profile.current_preset = preset;
    game->runtime = SC_GAME_RUNTIME_MAPPED;
    sc_game_apply_profile(game);
    apply_runtime_state(game);
    LOGI("Game mode: preset '%s'", game->profile.presets[preset].name);
    return true;
}

void
sc_game_select_native(struct sc_game *game) {
    game->runtime = SC_GAME_RUNTIME_NATIVE;
    apply_runtime_state(game);
    LOGI("Game mode: native scrplay controls");
}

void
sc_game_set_mode(struct sc_game *game, enum sc_game_mode mode) {
    if (mode == game->mode) {
        return;
    }
    if (game->mode == SC_GAME_MODE_EDIT) {
        // A crop edit previews the complete decoded frame. End it before any
        // save/apply or capture transition so gameplay can never inherit the
        // editor viewport, including when the mode changes by hotkey between
        // two video frames.
        if (game->ui.inited) {
            sc_game_ui_end_crop(&game->ui);
        }
        // Flush synchronously on return so the last drag/keystroke is durable
        // even if it happened inside the current UI frame, before the
        // debounced autosave detector runs.
        if (!sc_game_save_profile(game)) {
            // Keep the editor and its working copy intact so the error remains
            // visible and the user can retry instead of running unsaved state.
            return;
        }
        // The editor mutates the working profile, while the engine owns a
        // snapshot. Refresh it before enabling injection so edits take effect
        // immediately on returning to the game.
        sc_game_apply_profile(game);
    }
    game->mode = mode;
    sc_game_cancel_binding(game);
    game->ui.selected_element = -1;
    // drop any lingering editor focus (InputText active id): otherwise
    // io.WantTextInput stays true and every gameplay key gets swallowed
    if (game->ui.inited) {
        igClearActiveID();
    }
    apply_runtime_state(game);
    LOGI("Game mode: %s", mode == SC_GAME_MODE_EDIT ? "edit" : "play");
}

void
sc_game_toggle_mode(struct sc_game *game) {
    sc_game_set_mode(game, game->mode == SC_GAME_MODE_EDIT
                               ? SC_GAME_MODE_PLAY
                               : SC_GAME_MODE_EDIT);
}

void
sc_game_apply_profile(struct sc_game *game) {
    if (game->engine_initialized) {
        sc_game_engine_set_profile(&game->engine, &game->profile);
    }
    if (game->screen && game->screen->video) {
        const struct sc_game_crop *crop = &game->profile.crop;
        bool preview = game->ui.inited && game->ui.crop_editing;
        sc_screen_set_crop(game->screen, crop->enabled, crop->left, crop->top,
                           crop->right, crop->bottom, preview);
    }
}

void
sc_game_request_capture(struct sc_game *game, bool capture) {
    if (!game->screen) {
        sc_game_engine_set_captured(&game->engine, false);
        return;
    }
    sc_mouse_capture_set_active(&game->screen->mc, capture);
    // SDL may reject relative mode (for example while the window lacks
    // focus). Drive the aim finger from the resulting state, not merely from
    // the requested one, so capture can never leave an invisible touch down.
    sc_game_engine_set_captured(
        &game->engine,
        sc_mouse_capture_is_active(&game->screen->mc));
}

bool
sc_game_is_captured(struct sc_game *game) {
    if (!game->screen) {
        return false;
    }
    return sc_mouse_capture_is_active(&game->screen->mc);
}

void
sc_game_arm_binding(struct sc_game *game, struct sc_game_key *key) {
    sc_game_cancel_binding(game);
    if (!key || game->profile.current_preset >= game->profile.preset_count) {
        return;
    }

    struct sc_game_preset *preset =
        &game->profile.presets[game->profile.current_preset];
    for (size_t i = 0; i < preset->element_count; ++i) {
        struct sc_game_element *elem = &preset->elements[i];
        enum sc_game_binding_slot slot = SC_GAME_BINDING_NONE;
        if (elem->type == SC_GAME_ELEMENT_JOYSTICK) {
            if (key == &elem->joystick.up) {
                slot = SC_GAME_BINDING_JOYSTICK_UP;
            } else if (key == &elem->joystick.down) {
                slot = SC_GAME_BINDING_JOYSTICK_DOWN;
            } else if (key == &elem->joystick.left) {
                slot = SC_GAME_BINDING_JOYSTICK_LEFT;
            } else if (key == &elem->joystick.right) {
                slot = SC_GAME_BINDING_JOYSTICK_RIGHT;
            }
        } else if (elem->type == SC_GAME_ELEMENT_BIND
                && key == &elem->bind.key) {
            slot = SC_GAME_BINDING_BIND;
        } else if (elem->type == SC_GAME_ELEMENT_SWIPE
                && key == &elem->swipe.key) {
            slot = SC_GAME_BINDING_SWIPE;
        }
        if (slot != SC_GAME_BINDING_NONE) {
            game->binding_slot = slot;
            game->binding_preset = game->profile.current_preset;
            game->binding_element = i;
            LOGI("Game mode: press any key or mouse button to bind");
            return;
        }
    }
    LOGW("Game mode: refusing to arm an invalid binding target");
}

void
sc_game_cancel_binding(struct sc_game *game) {
    game->binding_slot = SC_GAME_BINDING_NONE;
    game->binding_preset = 0;
    game->binding_element = 0;
}

static struct sc_game_key *
binding_target(struct sc_game *game) {
    if (game->binding_slot == SC_GAME_BINDING_NONE
            || game->binding_preset >= game->profile.preset_count) {
        return NULL;
    }
    struct sc_game_preset *preset =
        &game->profile.presets[game->binding_preset];
    if (game->binding_element >= preset->element_count) {
        return NULL;
    }
    struct sc_game_element *elem = &preset->elements[game->binding_element];
    switch (game->binding_slot) {
        case SC_GAME_BINDING_JOYSTICK_UP:
            return elem->type == SC_GAME_ELEMENT_JOYSTICK
                 ? &elem->joystick.up : NULL;
        case SC_GAME_BINDING_JOYSTICK_DOWN:
            return elem->type == SC_GAME_ELEMENT_JOYSTICK
                 ? &elem->joystick.down : NULL;
        case SC_GAME_BINDING_JOYSTICK_LEFT:
            return elem->type == SC_GAME_ELEMENT_JOYSTICK
                 ? &elem->joystick.left : NULL;
        case SC_GAME_BINDING_JOYSTICK_RIGHT:
            return elem->type == SC_GAME_ELEMENT_JOYSTICK
                 ? &elem->joystick.right : NULL;
        case SC_GAME_BINDING_BIND:
            return elem->type == SC_GAME_ELEMENT_BIND
                 ? &elem->bind.key : NULL;
        case SC_GAME_BINDING_SWIPE:
            return elem->type == SC_GAME_ELEMENT_SWIPE
                 ? &elem->swipe.key : NULL;
        case SC_GAME_BINDING_NONE:
            return NULL;
    }
    return NULL;
}

bool
sc_game_is_binding_target(const struct sc_game *game,
                          const struct sc_game_key *key) {
    return binding_target((struct sc_game *) game) == key;
}

// ---- event hook ------------------------------------------------------------

static void
sync_frame_size(struct sc_game *game) {
    struct sc_screen *screen = game->screen;
    struct sc_size fs = screen->frame_size;
    if (!fs.width || !fs.height) {
        return;
    }
    // Relative mouse motion is already converted from window points to
    // oriented frame pixels by aim_delta_to_frame(). Passing the content/
    // HiDPI scale again here made the engine multiply every delta twice.
    sc_game_engine_set_frame_size(&game->engine, fs, 1.0f, 1.0f);
}

static void
handle_edit_keys(struct sc_game *game, const SDL_Event *event) {
    ImGuiIO *io = game->ui.inited ? igGetIO_Nil() : NULL;

    struct sc_game_key *target = binding_target(game);
    if (game->binding_slot != SC_GAME_BINDING_NONE && !target) {
        sc_game_cancel_binding(game);
        return;
    }
    if (target) {
        if (event->type == SDL_KEYDOWN && !event->key.repeat) {
            SDL_Keycode sym = event->key.keysym.sym;
            if (sym == SDLK_ESCAPE) {
                sc_game_cancel_binding(game);
                return;
            }
            target->type = SC_GAME_KEY_KEYBOARD;
            target->value = sym;
            sc_game_cancel_binding(game);
            sc_game_mark_profile_dirty(game);
        } else if (event->type == SDL_MOUSEBUTTONDOWN) {
            enum sc_mouse_button button =
                sc_mouse_button_from_sdl(event->button.button);
            if (button != SC_MOUSE_BUTTON_UNKNOWN) {
                target->type = SC_GAME_KEY_MOUSE;
                target->value = button;
                sc_game_cancel_binding(game);
                sc_game_mark_profile_dirty(game);
            }
        }
        return;
    }

    // Delete removes the selected element
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_DELETE
            && (!io || !io->WantTextInput)) {
        struct sc_game_preset *preset =
            &game->profile.presets[game->profile.current_preset];
        int idx = game->ui.selected_element;
        if (idx >= 0 && idx < (int) preset->element_count) {
            sc_game_cancel_binding(game);
            size_t uidx = (size_t) idx;
            memmove(&preset->elements[uidx], &preset->elements[uidx + 1],
                    (preset->element_count - uidx - 1)
                        * sizeof(preset->elements[0]));
            --preset->element_count;
            game->ui.selected_element = -1;
            sc_game_mark_profile_dirty(game);
        }
    }
}

bool
sc_game_handle_event(struct sc_game *game, const SDL_Event *event) {
    if (!game->screen || !game->ui.inited) {
        return false;
    }

    if (event->type == SC_EVENT_NEW_FRAME) {
        sync_frame_size(game);
        return false;
    }

    // Once Alt+left-button acquired a finger, its motion and UP must win over
    // ImGui, picker and mode checks. Otherwise releasing Alt first could route
    // the physical UP through mouse_capture and leave Android stuck pressed.
    if (event->type == SDL_MOUSEBUTTONUP) {
        enum sc_mouse_button button =
            sc_mouse_button_from_sdl(event->button.button);
        if (sc_game_direct_touch_button_up(&game->direct_touch,
                                           game->controller, button)) {
            recapture_after_direct_touch(game);
            return true;
        }
    } else if (event->type == SDL_MOUSEMOTION
            && game->direct_touch.active) {
        struct sc_position position;
        if (sc_screen_window_to_frame_position(game->screen,
                                               event->motion.x,
                                               event->motion.y, &position)) {
            sc_game_direct_touch_move(&game->direct_touch, game->controller,
                                      position);
        }
        return true;
    }

    // always feed ImGui first
    sc_game_ui_process_event(&game->ui, event, false);

    // Focus loss: keys and virtual touches held at that moment would stick
    // forever (SDL never delivers the matching UP after focus is gone).
    // Drop everything; the user re-presses after refocusing anyway.
    if (event->type == SDL_WINDOWEVENT
            && event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        release_direct_touch(game);
        game->alt_pressed = 0;
        game->consumed_hotkey = SDLK_UNKNOWN;
        game->hotkey_text_pending = false;
        game->preset_hotkey_held = false;
        game->preset_hotkey_long_triggered = false;
        game->preset_hotkey_deadline = 0;
        game->mapped_key_count = 0;
        sc_input_manager_release_all(&game->screen->im);
        sc_game_engine_release_all(&game->engine);
        sc_game_engine_set_mods(&game->engine, 0);
        // sc_screen_handle_event() returns directly for window events, so its
        // generic mouse-capture handler never sees FOCUS_LOST.
        sc_game_request_capture(game, false);
        sync_text_input(game);
        return false;
    }

    bool edit_mode = game->mode == SC_GAME_MODE_EDIT;
    ImGuiIO *io = igGetIO_Nil();

    switch (event->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            // Consume both halves of a hotkey at this pre-input-manager layer.
            // Otherwise the key-down changes mode but its key-up may leak to
            // Android, leaving an unmatched release in the real processor.
            if (event->key.keysym.sym == game->consumed_hotkey) {
                if (event->type == SDL_KEYUP) {
                    if (game->preset_hotkey_held) {
                        // A stalled render must not turn an already-long hold
                        // into a short press merely because no timer frame ran.
                        update_preset_hotkey_hold(game);
                        if (!game->preset_hotkey_long_triggered) {
                            activate_short_preset_hotkey(game);
                        }
                        game->preset_hotkey_held = false;
                        game->preset_hotkey_long_triggered = false;
                        game->preset_hotkey_deadline = 0;
                    }
                    game->consumed_hotkey = SDLK_UNKNOWN;
                    game->hotkey_text_pending = false;
                }
                return true; // also discard auto-repeated hotkey key-downs
            }
            if (game->picker_blocking) {
                return true;
            }
            if (event->type == SDL_KEYDOWN && !event->key.repeat
                    && game->binding_slot == SC_GAME_BINDING_NONE
                    && !io->WantTextInput) {
                const struct sc_game_hotkey *edit =
                    &game->profile.hotkey_edit;
                if (hotkey_matches(edit, event->key.keysym.sym,
                                   event->key.keysym.mod)) {
                    game->consumed_hotkey = event->key.keysym.sym;
                    game->hotkey_text_pending =
                        event->key.keysym.sym >= 0x20
                        && event->key.keysym.sym < 0x7f;
                    sc_game_toggle_mode(game);
                    return true;
                }
                const struct sc_game_hotkey *preset =
                    &game->profile.hotkey_next_preset;
                if (!edit_mode
                        && hotkey_matches(preset, event->key.keysym.sym,
                                          event->key.keysym.mod)) {
                    game->consumed_hotkey = event->key.keysym.sym;
                    game->hotkey_text_pending =
                        event->key.keysym.sym >= 0x20
                        && event->key.keysym.sym < 0x7f;
                    game->preset_hotkey_held = true;
                    game->preset_hotkey_long_triggered = false;
                    game->preset_hotkey_deadline =
                        sc_tick_now() + SC_GAME_NATIVE_HOLD_DELAY;
                    return true;
                }
            }
            if (edit_mode) {
                handle_edit_keys(game, event);
                return true;
            }
            // Hold-Alt capture model: side effects only. The event KEEPS
            // flowing to the key processor, which owns modifier tracking and
            // never forwards the capture modifier to the device. Consuming
            // it here would leave the engine's modifier state stuck (and
            // combo keys misfiring).
            if (game->runtime == SC_GAME_RUNTIME_MAPPED
                    && (event->key.keysym.sym == SDLK_LALT
                        || event->key.keysym.sym == SDLK_RALT)) {
                uint8_t bit = event->key.keysym.sym == SDLK_LALT ? 1 : 2;
                uint8_t previous = game->alt_pressed;
                if (event->type == SDL_KEYDOWN) {
                    game->alt_pressed |= bit;
                    if (!previous && sc_game_is_captured(game)) {
                        sc_game_request_capture(game, false);
                    }
                } else {
                    game->alt_pressed &= ~bit;
                    if (previous && !game->alt_pressed
                            && !game->direct_touch.awaiting_button_up
                            && !sc_game_is_captured(game)
                            && !io->WantCaptureMouse) {
                        // Modifier-dependent mappings are no longer valid.
                        // Clear their pressed state even if their base-key UP
                        // arrives after this final Alt release.
                        sc_game_engine_release_all(&game->engine);
                        sc_game_request_capture(game, true);
                    }
                }
                return false;
            }
            // Only steal keys when ImGui is actually typing (picker input;
            // the editor returned above). The flag can stick after leaving
            // the editor (a lingering ActiveId) and must never eat
            // gameplay keys.
            if (game->ui.picker_open && io->WantTextInput) {
                return true;
            }
            return false;
        case SDL_TEXTINPUT:
            if (game->hotkey_text_pending) {
                game->hotkey_text_pending = false;
                return true;
            }
            if (printable_mapped_key_held(game)) {
                return true;
            }
            return game->picker_blocking || edit_mode || io->WantTextInput
                || game->alt_pressed;
        case SDL_MOUSEMOTION:
        case SDL_MOUSEWHEEL:
            // while Alt is held, the mouse belongs to the desktop: keep mc
            // from click-recapturing and keep the hover timer idle
            return game->picker_blocking || edit_mode || io->WantCaptureMouse
                || game->alt_pressed;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (event->type == SDL_MOUSEBUTTONDOWN
                    && game->runtime == SC_GAME_RUNTIME_MAPPED
                    && game->alt_pressed
                    && event->button.button == SDL_BUTTON_LEFT
                    && !io->WantCaptureMouse) {
                struct sc_position position;
                if (sc_screen_window_to_frame_position(
                        game->screen, event->button.x, event->button.y,
                        &position)) {
                    sc_game_direct_touch_press(&game->direct_touch,
                                               game->controller,
                                               SC_MOUSE_BUTTON_LEFT,
                                               position);
                }
                // The physical press belongs to direct-touch even if it was
                // over a black bar or the controller queue rejected DOWN: do
                // not let mouse_capture turn it into a surprise recapture.
                return true;
            }
            if (game->picker_blocking) {
                return true;
            }
            if (edit_mode) {
                handle_edit_keys(game, event);
                return true;
            }
            return io->WantCaptureMouse || game->alt_pressed;
        case SDL_FINGERMOTION:
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
            return game->picker_blocking || edit_mode;
        default:
            return false;
    }
}

// ---- render hook -----------------------------------------------------------

// While playing and not captured: if the cursor lingers inside the game
// area (and not over an ImGui panel), recapture it. This makes Alt-release
// a deliberate "throw the mouse to the desktop" gesture: lingering inside
// the window snaps the mouse back into the game.
static void
update_hover_recapture(struct sc_game *game) {
    static sc_tick released_at; // 0 = not in released state
    bool captured = sc_game_is_captured(game);

    if (game->mode != SC_GAME_MODE_PLAY || game->picker_blocking
            || game->runtime != SC_GAME_RUNTIME_MAPPED) {
        released_at = 0;
        return;
    }
    if (captured) {
        released_at = 0;
        return;
    }
    if (game->alt_pressed || game->direct_touch.awaiting_button_up) {
        released_at = 0; // user is actively using the free mouse
        return;
    }
    if (!released_at) {
        released_at = sc_tick_now();
        return;
    }
    if (sc_tick_now() - released_at < SC_TICK_FROM_MS(2500)) {
        return; // grace period to move the mouse out or onto the HUD
    }
    ImGuiIO *io = game->ui.inited ? igGetIO_Nil() : NULL;
    if (io && io->WantCaptureMouse) {
        return; // hovering the HUD/panel: keep the mouse free
    }
    SDL_Window *window = game->screen->window;
    if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS)) {
        return; // cursor is outside the window: stay free
    }
    sc_game_request_capture(game, true);
    released_at = 0;
}

static void
game_render_cb(SDL_Renderer *renderer, void *userdata) {
    (void) renderer;
    struct sc_game *game = userdata;
    // SC_EVENT_NEW_FRAME is offered to the game pre-hook before screen.c
    // installs the new frame and updates screen->frame_size.  Refresh here,
    // after the video frame has actually been applied, so the engine never
    // renders/injects one frame with stale dimensions after a rotation.
    sync_frame_size(game);
    update_preset_hotkey_hold(game);
    // screen.c also owns a few generic capture transitions (initial capture
    // and click-to-recapture). Mirror their actual SDL state into the engine
    // here; set_captured() is idempotent and presses the aim start point as
    // soon as capture, frame size and the active preset are all ready.
    sc_game_engine_set_captured(&game->engine, sc_game_is_captured(game));
    update_hover_recapture(game);
    sc_game_ui_render(&game->ui);
    autosave_profile_if_due(game);
    if (game->picker_blocking && !game->ui.picker_open) {
        // Loading and closing the modal happen during sc_game_ui_render().
        // Restore the selected runtime on the same UI thread once it is gone.
        game->picker_blocking = false;
        sc_game_apply_profile(game);
        apply_runtime_state(game);
        if (game->runtime == SC_GAME_RUNTIME_MAPPED) {
            sc_game_request_capture(game, true);
        }
    }
    sync_text_input(game);
}

// ---- lifecycle -------------------------------------------------------------

bool
sc_game_init(struct sc_game *game, struct sc_controller *controller,
             struct sc_key_processor *real_kp,
             struct sc_mouse_processor *real_mp, const char *profile_name,
             const char *font_path, float ui_scale_override) {
    memset(game, 0, sizeof(*game));
    // must be set before the window is created: without it SDL2's Wayland
    // driver never engages the text-input protocol, so the IME (fcitx5 & co)
    // cannot be activated in the editor's name fields
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    game->controller = controller;
    game->real_kp = real_kp;
    game->real_mp = real_mp;
    game->screen = NULL;
    game->mode = SC_GAME_MODE_PLAY;
    game->runtime = SC_GAME_RUNTIME_MAPPED;
    game->profile_revision = 0;
    game->profile_loaded = false;
    game->profile_dirty = false;
    game->save_status = SC_GAME_SAVE_STATUS_SAVED;
    game->autosave_deadline = 0;
    game->engine_initialized = false;
    game->picker_blocking = false;
    game->alt_pressed = 0;
    sc_game_direct_touch_init(&game->direct_touch);
    game->native_mouse_capture_keys = 0;
    sc_game_cancel_binding(game);
    game->consumed_hotkey = SDLK_UNKNOWN;
    game->hotkey_text_pending = false;
    game->preset_hotkey_held = false;
    game->preset_hotkey_long_triggered = false;
    game->preset_hotkey_deadline = 0;
    game->mapped_key_count = 0;

    game->ui_scale = 1.0f;
    if (ui_scale_override > 0.0f) {
        game->ui_scale = ui_scale_override;
    } else {
        game->ui_scale = detect_desktop_scale();
    }
    if (game->ui_scale != 1.0f) {
        LOGI("Game mode: UI scale %.2f", game->ui_scale);
    }

    if (!build_config_paths(game)) {
        return false;
    }
#ifndef _WIN32
    // populate the new config dir from the pre-rename one, if any
    sc_game_migrate_config_dir();
#endif
    enum sc_game_storage_status storage_status =
        sc_game_storage_create_dir(game->profiles_dir);
    if (storage_status != SC_GAME_STORAGE_OK) {
        LOGE("Game mode: could not create '%s': %s", game->profiles_dir,
             sc_game_storage_status_to_string(storage_status));
        return false;
    }

    if (font_path) {
        snprintf(game->font_path, sizeof(game->font_path), "%s", font_path);
    } else {
        game->font_path[0] = '\0';
    }

    // Defaults are also used when no profile could be loaded. Initialize the
    // engine before any load/create helper applies a profile: those helpers
    // take the engine mutex.
    sc_game_profile_init_default(&game->profile, "default");

    if (!sc_game_engine_init(&game->engine, controller)) {
        return false;
    }
    game->engine_initialized = true;

    if (profile_name && *profile_name) {
        if (!sc_game_load_profile(game, profile_name)) {
            LOGW("Game mode: profile '%s' not found", profile_name);
        }
    } else {
        char names[32][SC_GAME_PROFILE_NAME_MAX];
        size_t count = sc_game_list_profiles(game, names, 32);
        if (count == 0) {
            // first run: create the default ARPG-oriented profile
            if (sc_game_create_profile(game, "default")) {
                sc_game_load_profile(game, "default");
                LOGI("Game mode: default profile created");
            }
        } else if (count == 1) {
            sc_game_load_profile(game, names[0]);
        }
        // count > 1: defer to the in-window picker
    }

    // trait setup
    game->kp_trait.ops = &key_processor_ops;
    game->kp_trait.async_paste = real_kp ? real_kp->async_paste : false;
    game->kp_trait.hid = real_kp ? real_kp->hid : false;
    game->mp_trait.ops = &mouse_processor_ops;
    game->mp_trait.relative_mode = true; // play mode

    sc_game_apply_profile(game);
    if (!sc_game_engine_start(&game->engine)) {
        sc_game_engine_destroy(&game->engine);
        game->engine_initialized = false;
        return false;
    }
    // game->mode starts in PLAY, so there is no mode transition to activate
    // the engine on first launch. Do it explicitly.
    sc_game_engine_set_active(&game->engine, true);
    return true;
}

bool
sc_game_set_screen(struct sc_game *game, struct sc_screen *screen) {
    game->screen = screen;
    // screen.c initialized this from regular scrplay options. Mapped runtime
    // disables it, while Native restores the exact original behavior.
    game->native_mouse_capture_keys = screen->mc.sdl_mouse_capture_keys;

    if (!sc_game_ui_init(&game->ui, game, screen->window,
                         screen->display.renderer,
                         game->font_path[0] ? game->font_path : NULL,
                         game->ini_path)) {
        return false;
    }
    // Profiles may be loaded before the screen exists. Apply their persistent
    // client viewport now; before the first frame, screen.c simply retains the
    // normalized rectangle and resolves it once the decoded size is known.
    sc_game_apply_profile(game);
    game->ui.picker_open = !game->profile_loaded;
    if (game->ui.picker_open) {
        // Keep the cursor usable and injection disabled while the modal is
        // waiting. This is not edit mode: closing the picker resumes play,
        // matching its own UI hint and avoiding an editor flash behind it.
        game->picker_blocking = true;
    }

    apply_runtime_state(game);

    // SDL starts text input implicitly on desktop platforms. apply_runtime
    // disables it immediately for mapped play, before the first gameplay key.
    sync_text_input(game);

    sc_display_set_overlay_callback(&screen->display, game_render_cb, game);
    sync_frame_size(game);
    sc_game_engine_set_captured(&game->engine,
                                sc_mouse_capture_is_active(&screen->mc));
    return true;
}

void
sc_game_destroy(struct sc_game *game) {
    // The display outlives the game object (and therefore the ImGui context).
    // Detach first so no later render pass can call game_render_cb() with
    // destroyed UI/engine state.
    if (game->screen) {
        sc_display_set_overlay_callback(&game->screen->display, NULL, NULL);
    }
    if (SDL_IsTextInputActive()) {
#if SDL_VERSION_ATLEAST(2, 0, 22)
        SDL_ClearComposition();
#endif
        SDL_StopTextInput();
    }
    release_direct_touch(game);
    if (game->profile_dirty) {
        sc_game_save_profile(game);
    }
    if (game->engine_initialized) {
        sc_game_engine_destroy(&game->engine);
        game->engine_initialized = false;
    }
    sc_game_ui_destroy(&game->ui);
}
