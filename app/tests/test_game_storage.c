#include "common.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <windows.h>
# include <wchar.h>
#else
# include <dirent.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

#include "game/game_profile.h"
#include "game/game_storage.h"

struct test_dir {
    char path[1024];
};

#ifdef _WIN32
static wchar_t *
to_wide(const char *utf8) {
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                                  NULL, 0);
    assert(len > 0);
    wchar_t *wide = malloc((size_t) len * sizeof(*wide));
    assert(wide);
    assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide,
                               len));
    return wide;
}
#endif

static void
make_test_dir(struct test_dir *dir) {
#ifdef _WIN32
    wchar_t temp_root[MAX_PATH + 1];
    DWORD len = GetTempPathW(MAX_PATH, temp_root);
    assert(len && len <= MAX_PATH);
    wchar_t temp_file[MAX_PATH + 1];
    assert(GetTempFileNameW(temp_root, L"sgs", 0, temp_file));
    assert(DeleteFileW(temp_file));
    assert(CreateDirectoryW(temp_file, NULL));
    int utf8_len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                       temp_file, -1, dir->path,
                                       sizeof(dir->path), NULL, NULL);
    assert(utf8_len > 0);
#else
    snprintf(dir->path, sizeof(dir->path), "/tmp/scrplay-storage-XXXXXX");
    assert(mkdtemp(dir->path));
#endif
}

static char *
join_path(const char *dir, const char *filename) {
    size_t dir_len = strlen(dir);
    size_t filename_len = strlen(filename);
    char *path = malloc(dir_len + filename_len + 2);
    assert(path);
#ifdef _WIN32
    snprintf(path, dir_len + filename_len + 2, "%s\\%s", dir, filename);
#else
    snprintf(path, dir_len + filename_len + 2, "%s/%s", dir, filename);
#endif
    return path;
}

static void
write_raw_file(const char *path, const void *data, size_t size) {
#ifdef _WIN32
    wchar_t *wide = to_wide(path);
    FILE *file = _wfopen(wide, L"wb");
    free(wide);
#else
    FILE *file = fopen(path, "wb");
#endif
    assert(file);
    assert(fwrite(data, 1, size, file) == size);
    assert(!fclose(file));
}

static char *
read_raw_file(const char *path, size_t *size_out) {
#ifdef _WIN32
    wchar_t *wide = to_wide(path);
    FILE *file = _wfopen(wide, L"rb");
    free(wide);
#else
    FILE *file = fopen(path, "rb");
#endif
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size >= 0);
    assert(!fseek(file, 0, SEEK_SET));
    char *data = malloc((size_t) size + 1);
    assert(data);
    assert(fread(data, 1, (size_t) size, file) == (size_t) size);
    assert(!fclose(file));
    data[size] = '\0';
    *size_out = (size_t) size;
    return data;
}

static void
remove_file(const char *path) {
#ifdef _WIN32
    wchar_t *wide = to_wide(path);
    assert(DeleteFileW(wide));
    free(wide);
#else
    assert(!unlink(path));
#endif
}

static void
make_directory(const char *path) {
#ifdef _WIN32
    wchar_t *wide = to_wide(path);
    assert(CreateDirectoryW(wide, NULL));
    free(wide);
#else
    assert(!mkdir(path, 0700));
#endif
}

static void
remove_directory(const char *path) {
#ifdef _WIN32
    wchar_t *wide = to_wide(path);
    assert(RemoveDirectoryW(wide));
    free(wide);
#else
    assert(!rmdir(path));
#endif
}

static void
assert_no_temp_files(const char *directory) {
#ifdef _WIN32
    char *pattern = join_path(directory, "*.tmp.*");
    wchar_t *wide_pattern = to_wide(pattern);
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(wide_pattern, &data);
    assert(find == INVALID_HANDLE_VALUE);
    assert(GetLastError() == ERROR_FILE_NOT_FOUND);
    free(wide_pattern);
    free(pattern);
#else
    DIR *dir = opendir(directory);
    assert(dir);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        assert(!strstr(entry->d_name, ".tmp."));
    }
    assert(!closedir(dir));
#endif
}

static void
delete_named_profile(const char *directory, const char *name) {
    enum sc_game_storage_status status =
        sc_game_storage_delete(directory, name);
    assert(status == SC_GAME_STORAGE_OK);
}

static void
test_create_save_load_unicode(void) {
    struct test_dir root;
    make_test_dir(&root);
    char *level1 = join_path(root.path, "游戏配置");
    char *profiles = join_path(level1, "profiles");
    assert(sc_game_storage_create_dir(profiles) == SC_GAME_STORAGE_OK);
    assert(sc_game_storage_create_dir(profiles) == SC_GAME_STORAGE_OK);

    struct sc_game_profile expected;
    sc_game_profile_init_default(&expected, "异环");
    assert(sc_game_storage_save(profiles, &expected) == SC_GAME_STORAGE_OK);

    struct sc_game_profile loaded;
    size_t error_line = 999;
    assert(sc_game_storage_load(profiles, "异环", &loaded, &error_line)
           == SC_GAME_STORAGE_OK);
    assert(!error_line);
    assert(!strcmp(loaded.name, expected.name));
    assert(loaded.preset_count == expected.preset_count);
    assert(loaded.current_preset == 0);

    delete_named_profile(profiles, "异环");
    remove_directory(profiles);
    remove_directory(level1);
    remove_directory(root.path);
    free(profiles);
    free(level1);
}

static void
test_strict_read_guards(void) {
    struct test_dir dir;
    make_test_dir(&dir);

    static const char mismatch[] =
        "format 2\nprofile inside\nhotkey edit=shift+e preset=f8\n"
        "crop off\n"
        "preset default\naim off sens=1.00\nhumanize off amp=0.0000\n";
    char *mismatch_path = join_path(dir.path, "outside.profile");
    write_raw_file(mismatch_path, mismatch, sizeof(mismatch) - 1);
    struct sc_game_profile profile;
    assert(sc_game_storage_load(dir.path, "outside", &profile, NULL)
           == SC_GAME_STORAGE_NAME_MISMATCH);

    static const char future[] =
        "format 3\nprofile future\nhotkey edit=shift+e preset=f8\n"
        "crop off\n"
        "preset default\naim off sens=1.00\nhumanize off amp=0.0000\n";
    char *future_path = join_path(dir.path, "future.profile");
    write_raw_file(future_path, future, sizeof(future) - 1);
    size_t line = 0;
    assert(sc_game_storage_load(dir.path, "future", &profile, &line)
           == SC_GAME_STORAGE_INVALID_PROFILE);
    assert(line == 1);

    static const char with_nul[] =
        "format 2\nprofile nul\0\nhotkey edit=shift+e preset=f8\n";
    char *nul_path = join_path(dir.path, "nul.profile");
    write_raw_file(nul_path, with_nul, sizeof(with_nul) - 1);
    assert(sc_game_storage_load(dir.path, "nul", &profile, NULL)
           == SC_GAME_STORAGE_CONTAINS_NUL);

    char *large_path = join_path(dir.path, "large.profile");
    size_t large_size = SC_GAME_STORAGE_MAX_FILE_SIZE + 1;
    char *large = malloc(large_size);
    assert(large);
    memset(large, 'x', large_size);
    write_raw_file(large_path, large, large_size);
    free(large);
    assert(sc_game_storage_load(dir.path, "large", &profile, NULL)
           == SC_GAME_STORAGE_TOO_LARGE);

    remove_file(large_path);
    remove_file(nul_path);
    remove_file(future_path);
    remove_file(mismatch_path);
    remove_directory(dir.path);
    free(large_path);
    free(nul_path);
    free(future_path);
    free(mismatch_path);
}

static void
test_list_filters_and_sorts_before_limit(void) {
    struct test_dir dir;
    make_test_dir(&dir);

    static const char *const valid_names[] = {"zeta", "beta", "alpha"};
    for (size_t i = 0; i < sizeof(valid_names) / sizeof(valid_names[0]); ++i) {
        struct sc_game_profile profile;
        sc_game_profile_init_default(&profile, valid_names[i]);
        assert(sc_game_storage_save(dir.path, &profile) == SC_GAME_STORAGE_OK);
    }

    char *invalid = join_path(dir.path, "invalid.profile");
    static const char invalid_text[] = "format 9\nprofile invalid\n";
    write_raw_file(invalid, invalid_text, sizeof(invalid_text) - 1);

    char *mismatch = join_path(dir.path, "mismatch.profile");
    static const char mismatch_text[] =
        "format 2\nprofile somebody_else\n"
        "hotkey edit=shift+e preset=f8\ncrop off\npreset default\n"
        "aim off sens=1.00\nhumanize off amp=0.0000\n";
    write_raw_file(mismatch, mismatch_text, sizeof(mismatch_text) - 1);

    char *directory_profile = join_path(dir.path, "directory.profile");
    make_directory(directory_profile);

#ifndef _WIN32
    char *link_profile = join_path(dir.path, "link.profile");
    char *alpha_profile = join_path(dir.path, "alpha.profile");
    assert(!symlink(alpha_profile, link_profile));
#endif

    char names[2][SC_GAME_PROFILE_NAME_MAX];
    size_t count = sc_game_storage_list(dir.path, names, 2);
    assert(count == 2);
    assert(!strcmp(names[0], "alpha"));
    assert(!strcmp(names[1], "beta"));
    assert(sc_game_storage_list(dir.path, NULL, 0) == 0);

#ifndef _WIN32
    remove_file(link_profile);
    free(alpha_profile);
    free(link_profile);
#endif
    remove_directory(directory_profile);
    remove_file(mismatch);
    remove_file(invalid);
    for (size_t i = 0; i < sizeof(valid_names) / sizeof(valid_names[0]); ++i) {
        delete_named_profile(dir.path, valid_names[i]);
    }
    remove_directory(dir.path);
    free(directory_profile);
    free(mismatch);
    free(invalid);
}

static void
test_atomic_failure_preserves_old_file(void) {
    struct test_dir dir;
    make_test_dir(&dir);
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "atomic");
    assert(sc_game_storage_save(dir.path, &profile) == SC_GAME_STORAGE_OK);

    char *path = join_path(dir.path, "atomic.profile");
    size_t old_size;
    char *old_bytes = read_raw_file(path, &old_size);

    profile.presets[0].aim.sensitivity = 2.5f;
    sc_game_storage_test_fail_next_save_before_replace();
    assert(sc_game_storage_save(dir.path, &profile)
           == SC_GAME_STORAGE_IO_ERROR);

    size_t current_size;
    char *current_bytes = read_raw_file(path, &current_size);
    assert(current_size == old_size);
    assert(!memcmp(current_bytes, old_bytes, old_size));
    assert_no_temp_files(dir.path);

    free(current_bytes);
    free(old_bytes);
    free(path);
    delete_named_profile(dir.path, "atomic");
    remove_directory(dir.path);
}

static void
test_save_replaces_same_name(void) {
    struct test_dir dir;
    make_test_dir(&dir);
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "replace");
    profile.presets[0].aim.sensitivity = 1.0f;
    assert(sc_game_storage_save(dir.path, &profile) == SC_GAME_STORAGE_OK);

    profile.presets[0].aim.sensitivity = 3.25f;
    assert(sc_game_storage_save(dir.path, &profile) == SC_GAME_STORAGE_OK);
    struct sc_game_profile loaded;
    assert(sc_game_storage_load(dir.path, "replace", &loaded, NULL)
           == SC_GAME_STORAGE_OK);
    assert(loaded.presets[0].aim.sensitivity == 3.25f);
    char names[2][SC_GAME_PROFILE_NAME_MAX];
    assert(sc_game_storage_list(dir.path, names, 2) == 1);
    assert(!strcmp(names[0], "replace"));

    delete_named_profile(dir.path, "replace");
    remove_directory(dir.path);
}

static void
test_invalid_arguments_and_delete_link(void) {
    struct test_dir dir;
    make_test_dir(&dir);
    struct sc_game_profile profile;
    sc_game_profile_init_default(&profile, "valid");
    assert(sc_game_storage_save(dir.path, &profile) == SC_GAME_STORAGE_OK);
    assert(sc_game_storage_load(dir.path, "../escape", &profile, NULL)
           == SC_GAME_STORAGE_INVALID_NAME);
    assert(sc_game_storage_delete(dir.path, "../escape")
           == SC_GAME_STORAGE_INVALID_NAME);
    assert(sc_game_storage_load(NULL, "valid", &profile, NULL)
           == SC_GAME_STORAGE_INVALID_ARGUMENT);
    assert(sc_game_storage_save(NULL, &profile)
           == SC_GAME_STORAGE_INVALID_ARGUMENT);

#ifndef _WIN32
    char *link = join_path(dir.path, "linked.profile");
    char *valid = join_path(dir.path, "valid.profile");
    assert(!symlink(valid, link));
    assert(sc_game_storage_delete(dir.path, "linked")
           == SC_GAME_STORAGE_IO_ERROR);
    // Rejecting the link must not delete its target.
    struct sc_game_profile loaded;
    assert(sc_game_storage_load(dir.path, "valid", &loaded, NULL)
           == SC_GAME_STORAGE_OK);
    remove_file(link);
    free(valid);
    free(link);
#endif

    delete_named_profile(dir.path, "valid");
    assert(sc_game_storage_delete(dir.path, "valid")
           == SC_GAME_STORAGE_NOT_FOUND);
    remove_directory(dir.path);
}

int
main(void) {
    test_create_save_load_unicode();
    test_strict_read_guards();
    test_list_filters_and_sorts_before_limit();
    test_atomic_failure_preserves_old_file();
    test_save_replaces_same_name();
    test_invalid_arguments_and_delete_link();
    return 0;
}
