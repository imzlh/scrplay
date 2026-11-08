#ifndef SC_GAME_STORAGE_H
#define SC_GAME_STORAGE_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>

#include "game_profile.h"

// Profile files are deliberately small text documents.  Keep a hard limit so
// a damaged (or hostile) file cannot make startup allocate an arbitrary amount
// of memory.
#define SC_GAME_STORAGE_MAX_FILE_SIZE (1024u * 1024u)

enum sc_game_storage_status {
    SC_GAME_STORAGE_OK,
    SC_GAME_STORAGE_INVALID_ARGUMENT,
    SC_GAME_STORAGE_INVALID_NAME,
    SC_GAME_STORAGE_NOT_FOUND,
    SC_GAME_STORAGE_TOO_LARGE,
    SC_GAME_STORAGE_CONTAINS_NUL,
    SC_GAME_STORAGE_INVALID_PROFILE,
    SC_GAME_STORAGE_NAME_MISMATCH,
    SC_GAME_STORAGE_OUT_OF_MEMORY,
    SC_GAME_STORAGE_IO_ERROR,
};

const char *
sc_game_storage_status_to_string(enum sc_game_storage_status status);

// Recursively create directory as a UTF-8 path. Existing directories are OK.
enum sc_game_storage_status
sc_game_storage_create_dir(const char *directory);

// Save profile as <directory>/<profile.name>.profile. The destination is
// replaced atomically only after all bytes have been written and flushed.
enum sc_game_storage_status
sc_game_storage_save(const char *directory,
                     const struct sc_game_profile *profile);

// Load <name>.profile. In addition to strict format parsing, the profile name
// stored in the file must exactly equal name.
enum sc_game_storage_status
sc_game_storage_load(const char *directory, const char *name,
                     struct sc_game_profile *profile, size_t *error_line);

// Delete a regular, non-link profile file.
enum sc_game_storage_status
sc_game_storage_delete(const char *directory, const char *name);

// List valid, regular, non-link format-1 profiles. All candidates are checked
// and sorted before max is applied, so a small result is deterministic.
size_t
sc_game_storage_list(const char *directory,
                     char names[][SC_GAME_PROFILE_NAME_MAX], size_t max);

#ifdef SC_TEST
// Deterministic fault injection for the atomic-replacement invariant.
void
sc_game_storage_test_fail_next_save_before_replace(void);
#endif

#endif
