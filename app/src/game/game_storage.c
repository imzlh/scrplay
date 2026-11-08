#include "game_storage.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <windows.h>
# include <wchar.h>
#else
# include <dirent.h>
# include <fcntl.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <unistd.h>
#endif

#define PROFILE_SUFFIX ".profile"
#define PROFILE_SUFFIX_LEN (sizeof(PROFILE_SUFFIX) - 1)

#ifdef SC_TEST
static bool fail_next_save_before_replace;

void
sc_game_storage_test_fail_next_save_before_replace(void) {
    fail_next_save_before_replace = true;
}

static bool
consume_save_failure(void) {
    bool fail = fail_next_save_before_replace;
    fail_next_save_before_replace = false;
    return fail;
}
#else
static bool
consume_save_failure(void) {
    return false;
}
#endif

const char *
sc_game_storage_status_to_string(enum sc_game_storage_status status) {
    switch (status) {
        case SC_GAME_STORAGE_OK:
            return "ok";
        case SC_GAME_STORAGE_INVALID_ARGUMENT:
            return "invalid argument";
        case SC_GAME_STORAGE_INVALID_NAME:
            return "invalid profile name";
        case SC_GAME_STORAGE_NOT_FOUND:
            return "not found";
        case SC_GAME_STORAGE_TOO_LARGE:
            return "profile is too large";
        case SC_GAME_STORAGE_CONTAINS_NUL:
            return "profile contains a NUL byte";
        case SC_GAME_STORAGE_INVALID_PROFILE:
            return "invalid profile";
        case SC_GAME_STORAGE_NAME_MISMATCH:
            return "profile name does not match its file name";
        case SC_GAME_STORAGE_OUT_OF_MEMORY:
            return "out of memory";
        case SC_GAME_STORAGE_IO_ERROR:
            return "I/O error";
        default:
            return "unknown storage error";
    }
}

static char *
build_profile_path(const char *directory, const char *name) {
    if (!directory || !directory[0] || !name) {
        return NULL;
    }

    size_t dir_len = strlen(directory);
    size_t name_len = strlen(name);
#ifdef _WIN32
    bool needs_separator = directory[dir_len - 1] != '\\'
                        && directory[dir_len - 1] != '/';
    const char separator = '\\';
#else
    bool needs_separator = directory[dir_len - 1] != '/';
    const char separator = '/';
#endif
    if (dir_len > SIZE_MAX - name_len - PROFILE_SUFFIX_LEN - 2) {
        return NULL;
    }
    size_t len = dir_len + needs_separator + name_len + PROFILE_SUFFIX_LEN;
    char *path = malloc(len + 1);
    if (!path) {
        return NULL;
    }
    memcpy(path, directory, dir_len);
    size_t pos = dir_len;
    if (needs_separator) {
        path[pos++] = separator;
    }
    memcpy(path + pos, name, name_len);
    pos += name_len;
    memcpy(path + pos, PROFILE_SUFFIX, PROFILE_SUFFIX_LEN + 1);
    return path;
}

static enum sc_game_storage_status
validate_load_arguments(const char *directory, const char *name,
                        const struct sc_game_profile *profile) {
    if (!directory || !directory[0] || !profile) {
        return SC_GAME_STORAGE_INVALID_ARGUMENT;
    }
    if (!sc_game_profile_name_is_valid(name)) {
        return SC_GAME_STORAGE_INVALID_NAME;
    }
    return SC_GAME_STORAGE_OK;
}

#ifdef _WIN32

static wchar_t *
utf8_to_wide(const char *utf8) {
    if (!utf8) {
        return NULL;
    }
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1,
                                  NULL, 0);
    if (!len) {
        return NULL;
    }
    wchar_t *wide = malloc((size_t) len * sizeof(*wide));
    if (!wide) {
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wide,
                             len)) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *
wide_to_utf8(const wchar_t *wide) {
    int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                  NULL, 0, NULL, NULL);
    if (!len) {
        return NULL;
    }
    char *utf8 = malloc((size_t) len);
    if (!utf8) {
        return NULL;
    }
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, utf8,
                             len, NULL, NULL)) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static bool
wide_is_separator(wchar_t c) {
    return c == L'\\' || c == L'/';
}

static bool
create_one_directory(const wchar_t *path) {
    if (CreateDirectoryW(path, NULL)) {
        return true;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES
        && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

enum sc_game_storage_status
sc_game_storage_create_dir(const char *directory) {
    if (!directory || !directory[0]) {
        return SC_GAME_STORAGE_INVALID_ARGUMENT;
    }
    wchar_t *path = utf8_to_wide(directory);
    if (!path) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }

    size_t len = wcslen(path);
    while (len > 1 && wide_is_separator(path[len - 1])) {
        path[--len] = L'\0';
    }

    wchar_t *p = path;
    if (len >= 2 && path[1] == L':') {
        p = path + 2;
        if (wide_is_separator(*p)) {
            ++p;
        }
    } else if (len >= 2 && wide_is_separator(path[0])
                         && wide_is_separator(path[1])) {
        // A UNC server and share are roots, not directories to create.
        p = path + 2;
        while (*p && !wide_is_separator(*p)) {
            ++p;
        }
        if (*p) {
            ++p;
        }
        while (*p && !wide_is_separator(*p)) {
            ++p;
        }
        if (*p) {
            ++p;
        }
    } else if (wide_is_separator(*p)) {
        ++p;
    }

    bool ok = true;
    for (; *p; ++p) {
        if (!wide_is_separator(*p)) {
            continue;
        }
        wchar_t saved = *p;
        *p = L'\0';
        if (path[0] && !create_one_directory(path)) {
            ok = false;
        }
        *p = saved;
        if (!ok) {
            break;
        }
    }
    if (ok) {
        ok = create_one_directory(path);
    }
    free(path);
    return ok ? SC_GAME_STORAGE_OK : SC_GAME_STORAGE_IO_ERROR;
}

static enum sc_game_storage_status
read_profile_file(const char *path, char **text_out, size_t *size_out) {
    wchar_t *wide_path = utf8_to_wide(path);
    if (!wide_path) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    HANDLE file = CreateFileW(wide_path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL
                                | FILE_FLAG_OPEN_REPARSE_POINT,
                              NULL);
    free(wide_path);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
             ? SC_GAME_STORAGE_NOT_FOUND : SC_GAME_STORAGE_IO_ERROR;
    }

    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file, &info)
            || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(file);
        return SC_GAME_STORAGE_IO_ERROR;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        return SC_GAME_STORAGE_IO_ERROR;
    }
    if ((uint64_t) size.QuadPart > SC_GAME_STORAGE_MAX_FILE_SIZE) {
        CloseHandle(file);
        return SC_GAME_STORAGE_TOO_LARGE;
    }

    size_t expected = (size_t) size.QuadPart;
    char *text = malloc(expected + 2);
    if (!text) {
        CloseHandle(file);
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    size_t total = 0;
    while (total < expected) {
        DWORD chunk;
        if (!ReadFile(file, text + total, (DWORD) (expected - total), &chunk,
                      NULL) || !chunk) {
            free(text);
            CloseHandle(file);
            return SC_GAME_STORAGE_IO_ERROR;
        }
        total += chunk;
    }
    char extra;
    DWORD extra_count = 0;
    bool read_extra = ReadFile(file, &extra, 1, &extra_count, NULL);
    bool close_ok = CloseHandle(file);
    if (!read_extra || extra_count || !close_ok) {
        free(text);
        return expected == SC_GAME_STORAGE_MAX_FILE_SIZE && extra_count
             ? SC_GAME_STORAGE_TOO_LARGE : SC_GAME_STORAGE_IO_ERROR;
    }
    if (memchr(text, '\0', expected)) {
        free(text);
        return SC_GAME_STORAGE_CONTAINS_NUL;
    }
    text[expected] = '\0';
    *text_out = text;
    *size_out = expected;
    return SC_GAME_STORAGE_OK;
}

static enum sc_game_storage_status
write_profile_atomic(const char *path, const char *text, size_t size) {
    wchar_t *destination = utf8_to_wide(path);
    if (!destination) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }

    size_t destination_len = wcslen(destination);
    size_t temp_cap = destination_len + 80;
    wchar_t *temp = malloc(temp_cap * sizeof(*temp));
    if (!temp) {
        free(destination);
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }

    HANDLE file = INVALID_HANDLE_VALUE;
    static volatile LONG temp_counter;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
        LONG counter = InterlockedIncrement(&temp_counter);
        int written = _snwprintf(temp, temp_cap, L"%ls.tmp.%lu.%lu.%ld",
                                 destination,
                                 (unsigned long) GetCurrentProcessId(),
                                 (unsigned long) GetTickCount(),
                                 (long) counter);
        if (written < 0 || (size_t) written >= temp_cap) {
            free(temp);
            free(destination);
            return SC_GAME_STORAGE_IO_ERROR;
        }
        file = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            break;
        }
        DWORD error = GetLastError();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
            free(temp);
            free(destination);
            return SC_GAME_STORAGE_IO_ERROR;
        }
    }
    if (file == INVALID_HANDLE_VALUE) {
        free(temp);
        free(destination);
        return SC_GAME_STORAGE_IO_ERROR;
    }

    bool ok = true;
    size_t total = 0;
    while (total < size) {
        DWORD chunk;
        DWORD request = (DWORD) MIN(size - total, (size_t) UINT32_MAX);
        if (!WriteFile(file, text + total, request, &chunk, NULL) || !chunk) {
            ok = false;
            break;
        }
        total += chunk;
    }
    if (ok && !FlushFileBuffers(file)) {
        ok = false;
    }
    if (!CloseHandle(file)) {
        ok = false;
    }
    if (ok && consume_save_failure()) {
        ok = false;
    }
    if (ok && !MoveFileExW(temp, destination,
                            MOVEFILE_REPLACE_EXISTING
                              | MOVEFILE_WRITE_THROUGH)) {
        ok = false;
    }
    if (!ok) {
        DeleteFileW(temp);
    }
    free(temp);
    free(destination);
    return ok ? SC_GAME_STORAGE_OK : SC_GAME_STORAGE_IO_ERROR;
}

#else // !_WIN32

static bool
create_one_directory(const char *path) {
    if (!mkdir(path, 0700)) {
        return true;
    }
    if (errno != EEXIST) {
        return false;
    }
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

enum sc_game_storage_status
sc_game_storage_create_dir(const char *directory) {
    if (!directory || !directory[0]) {
        return SC_GAME_STORAGE_INVALID_ARGUMENT;
    }
    char *path = strdup(directory);
    if (!path) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
    bool ok = true;
    for (char *p = path + (path[0] == '/'); *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (path[0] && !create_one_directory(path)) {
            ok = false;
        }
        *p = '/';
        if (!ok) {
            break;
        }
    }
    if (ok) {
        ok = create_one_directory(path);
    }
    free(path);
    return ok ? SC_GAME_STORAGE_OK : SC_GAME_STORAGE_IO_ERROR;
}

static enum sc_game_storage_status
read_profile_file(const char *path, char **text_out, size_t *size_out) {
    struct stat before;
    if (lstat(path, &before)) {
        return errno == ENOENT ? SC_GAME_STORAGE_NOT_FOUND
                              : SC_GAME_STORAGE_IO_ERROR;
    }
    if (!S_ISREG(before.st_mode)) {
        return SC_GAME_STORAGE_IO_ERROR;
    }

    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags);
    if (fd == -1) {
        return errno == ENOENT ? SC_GAME_STORAGE_NOT_FOUND
                              : SC_GAME_STORAGE_IO_ERROR;
    }
    struct stat after;
    if (fstat(fd, &after) || !S_ISREG(after.st_mode)
            || before.st_dev != after.st_dev || before.st_ino != after.st_ino
            || after.st_size < 0) {
        close(fd);
        return SC_GAME_STORAGE_IO_ERROR;
    }
    if ((uint64_t) after.st_size > SC_GAME_STORAGE_MAX_FILE_SIZE) {
        close(fd);
        return SC_GAME_STORAGE_TOO_LARGE;
    }

    size_t expected = (size_t) after.st_size;
    char *text = malloc(expected + 2);
    if (!text) {
        close(fd);
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    size_t total = 0;
    while (total < expected) {
        ssize_t count = read(fd, text + total, expected - total);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            free(text);
            close(fd);
            return SC_GAME_STORAGE_IO_ERROR;
        }
        total += (size_t) count;
    }
    char extra;
    ssize_t extra_count;
    do {
        extra_count = read(fd, &extra, 1);
    } while (extra_count < 0 && errno == EINTR);
    bool close_ok = !close(fd);
    if (extra_count != 0 || !close_ok) {
        free(text);
        return expected == SC_GAME_STORAGE_MAX_FILE_SIZE && extra_count > 0
             ? SC_GAME_STORAGE_TOO_LARGE : SC_GAME_STORAGE_IO_ERROR;
    }
    if (memchr(text, '\0', expected)) {
        free(text);
        return SC_GAME_STORAGE_CONTAINS_NUL;
    }
    text[expected] = '\0';
    *text_out = text;
    *size_out = expected;
    return SC_GAME_STORAGE_OK;
}

static void
sync_parent_directory(const char *path) {
    char *parent = strdup(path);
    if (!parent) {
        return;
    }
    char *slash = strrchr(parent, '/');
    if (slash) {
        if (slash == parent) {
            slash[1] = '\0';
        } else {
            *slash = '\0';
        }
    } else {
        strcpy(parent, ".");
    }
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
    int fd = open(parent, flags);
    if (fd != -1) {
        // Once rename() has succeeded it is unsafe to report a failure: the
        // new file is already visible and cannot be rolled back atomically.
        // This fsync is therefore durability best-effort.
        (void) fsync(fd);
        close(fd);
    }
    free(parent);
}

static enum sc_game_storage_status
write_profile_atomic(const char *path, const char *text, size_t size) {
    static const char suffix[] = ".tmp.XXXXXX";
    size_t path_len = strlen(path);
    if (path_len > SIZE_MAX - sizeof(suffix)) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    char *temp = malloc(path_len + sizeof(suffix));
    if (!temp) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    memcpy(temp, path, path_len);
    memcpy(temp + path_len, suffix, sizeof(suffix));

    int fd = mkstemp(temp);
    if (fd == -1) {
        free(temp);
        return SC_GAME_STORAGE_IO_ERROR;
    }
    (void) fchmod(fd, 0600);

    bool ok = true;
    size_t total = 0;
    while (total < size) {
        ssize_t count = write(fd, text + total, size - total);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            ok = false;
            break;
        }
        total += (size_t) count;
    }
    if (ok && fsync(fd)) {
        ok = false;
    }
    if (close(fd)) {
        ok = false;
    }
    if (ok && consume_save_failure()) {
        ok = false;
    }
    if (ok && rename(temp, path)) {
        ok = false;
    }
    if (!ok) {
        unlink(temp);
    } else {
        sync_parent_directory(path);
    }
    free(temp);
    return ok ? SC_GAME_STORAGE_OK : SC_GAME_STORAGE_IO_ERROR;
}

#endif // _WIN32

enum sc_game_storage_status
sc_game_storage_load(const char *directory, const char *name,
                     struct sc_game_profile *profile, size_t *error_line) {
    enum sc_game_storage_status status =
        validate_load_arguments(directory, name, profile);
    if (status != SC_GAME_STORAGE_OK) {
        return status;
    }
    if (error_line) {
        *error_line = 0;
    }
    char *path = build_profile_path(directory, name);
    if (!path) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    char *text;
    size_t size;
    status = read_profile_file(path, &text, &size);
    free(path);
    if (status != SC_GAME_STORAGE_OK) {
        return status;
    }
    (void) size;

    struct sc_game_profile loaded;
    size_t parse_error_line = 0;
    if (!sc_game_profile_parse(&loaded, text, &parse_error_line)) {
        free(text);
        if (error_line) {
            *error_line = parse_error_line;
        }
        return SC_GAME_STORAGE_INVALID_PROFILE;
    }
    free(text);
    if (strcmp(loaded.name, name)) {
        return SC_GAME_STORAGE_NAME_MISMATCH;
    }
    *profile = loaded;
    return SC_GAME_STORAGE_OK;
}

enum sc_game_storage_status
sc_game_storage_save(const char *directory,
                     const struct sc_game_profile *profile) {
    if (!directory || !directory[0] || !profile) {
        return SC_GAME_STORAGE_INVALID_ARGUMENT;
    }
    if (!sc_game_profile_name_is_valid(profile->name)) {
        return SC_GAME_STORAGE_INVALID_NAME;
    }
    char *text = sc_game_profile_serialize(profile);
    if (!text) {
        return SC_GAME_STORAGE_INVALID_PROFILE;
    }
    size_t size = strlen(text);
    if (size > SC_GAME_STORAGE_MAX_FILE_SIZE) {
        free(text);
        return SC_GAME_STORAGE_TOO_LARGE;
    }
    char *path = build_profile_path(directory, profile->name);
    if (!path) {
        free(text);
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    enum sc_game_storage_status status =
        write_profile_atomic(path, text, size);
    free(path);
    free(text);
    return status;
}

enum sc_game_storage_status
sc_game_storage_delete(const char *directory, const char *name) {
    if (!directory || !directory[0]) {
        return SC_GAME_STORAGE_INVALID_ARGUMENT;
    }
    if (!sc_game_profile_name_is_valid(name)) {
        return SC_GAME_STORAGE_INVALID_NAME;
    }
    char *path = build_profile_path(directory, name);
    if (!path) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
#ifdef _WIN32
    wchar_t *wide_path = utf8_to_wide(path);
    free(path);
    if (!wide_path) {
        return SC_GAME_STORAGE_OUT_OF_MEMORY;
    }
    DWORD attrs = GetFileAttributesW(wide_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        free(wide_path);
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
             ? SC_GAME_STORAGE_NOT_FOUND : SC_GAME_STORAGE_IO_ERROR;
    }
    if ((attrs & FILE_ATTRIBUTE_DIRECTORY)
            || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
        free(wide_path);
        return SC_GAME_STORAGE_IO_ERROR;
    }
    bool ok = DeleteFileW(wide_path);
    free(wide_path);
    return ok ? SC_GAME_STORAGE_OK : SC_GAME_STORAGE_IO_ERROR;
#else
    struct stat st;
    if (lstat(path, &st)) {
        enum sc_game_storage_status status =
            errno == ENOENT ? SC_GAME_STORAGE_NOT_FOUND
                            : SC_GAME_STORAGE_IO_ERROR;
        free(path);
        return status;
    }
    if (!S_ISREG(st.st_mode)) {
        free(path);
        return SC_GAME_STORAGE_IO_ERROR;
    }
    bool ok = !unlink(path);
    if (ok) {
        sync_parent_directory(path);
    }
    free(path);
    return ok ? SC_GAME_STORAGE_OK : SC_GAME_STORAGE_IO_ERROR;
#endif
}

struct profile_name {
    char value[SC_GAME_PROFILE_NAME_MAX];
};

static int
compare_profile_names(const void *a, const void *b) {
    const struct profile_name *left = a;
    const struct profile_name *right = b;
    return strcmp(left->value, right->value);
}

static bool
append_profile_name(struct profile_name **candidates, size_t *count,
                    size_t *capacity, const char *name) {
    if (*count == *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 16;
        if (new_capacity < *capacity
                || new_capacity > SIZE_MAX / sizeof(**candidates)) {
            return false;
        }
        struct profile_name *new_candidates =
            realloc(*candidates, new_capacity * sizeof(**candidates));
        if (!new_candidates) {
            return false;
        }
        *candidates = new_candidates;
        *capacity = new_capacity;
    }
    snprintf((*candidates)[*count].value,
             sizeof((*candidates)[*count].value), "%s", name);
    ++*count;
    return true;
}

static bool
has_profile_suffix(const char *name, size_t len) {
    return len > PROFILE_SUFFIX_LEN
        && !memcmp(name + len - PROFILE_SUFFIX_LEN, PROFILE_SUFFIX,
                   PROFILE_SUFFIX_LEN);
}

size_t
sc_game_storage_list(const char *directory,
                     char names[][SC_GAME_PROFILE_NAME_MAX], size_t max) {
    if (!directory || !directory[0] || (max && !names)) {
        return 0;
    }
    struct profile_name *candidates = NULL;
    size_t count = 0;
    size_t capacity = 0;

#ifdef _WIN32
    size_t dir_len = strlen(directory);
    bool needs_separator = directory[dir_len - 1] != '\\'
                        && directory[dir_len - 1] != '/';
    static const char wildcard[] = "*.profile";
    if (dir_len > SIZE_MAX - sizeof(wildcard) - needs_separator) {
        return 0;
    }
    char *pattern = malloc(dir_len + needs_separator + sizeof(wildcard));
    if (!pattern) {
        return 0;
    }
    memcpy(pattern, directory, dir_len);
    size_t pos = dir_len;
    if (needs_separator) {
        pattern[pos++] = '\\';
    }
    memcpy(pattern + pos, wildcard, sizeof(wildcard));
    wchar_t *wide_pattern = utf8_to_wide(pattern);
    free(pattern);
    if (!wide_pattern) {
        return 0;
    }
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(wide_pattern, &data);
    free(wide_pattern);
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        char *filename = wide_to_utf8(data.cFileName);
        if (!filename) {
            continue;
        }
        size_t len = strlen(filename);
        if (!has_profile_suffix(filename, len)) {
            free(filename);
            continue;
        }
        filename[len - PROFILE_SUFFIX_LEN] = '\0';
        if (sc_game_profile_name_is_valid(filename)) {
            struct sc_game_profile profile;
            if (sc_game_storage_load(directory, filename, &profile, NULL)
                    == SC_GAME_STORAGE_OK
                    && !append_profile_name(&candidates, &count, &capacity,
                                            filename)) {
                free(filename);
                break;
            }
        }
        free(filename);
    } while (FindNextFileW(find, &data));
    FindClose(find);
#else
    DIR *dir = opendir(directory);
    if (!dir) {
        return 0;
    }
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *filename = entry->d_name;
        size_t len = strlen(filename);
        if (!has_profile_suffix(filename, len)
                || len - PROFILE_SUFFIX_LEN >= SC_GAME_PROFILE_NAME_MAX) {
            continue;
        }
        char name[SC_GAME_PROFILE_NAME_MAX];
        memcpy(name, filename, len - PROFILE_SUFFIX_LEN);
        name[len - PROFILE_SUFFIX_LEN] = '\0';
        if (!sc_game_profile_name_is_valid(name)) {
            continue;
        }
        // load() performs lstat/open/fstat identity checks, rejecting links,
        // directories, races and invalid or mismatched profile contents.
        struct sc_game_profile profile;
        if (sc_game_storage_load(directory, name, &profile, NULL)
                != SC_GAME_STORAGE_OK) {
            continue;
        }
        if (!append_profile_name(&candidates, &count, &capacity, name)) {
            break;
        }
    }
    closedir(dir);
#endif

    if (count > 1) {
        qsort(candidates, count, sizeof(*candidates), compare_profile_names);
    }
    size_t result_count = MIN(count, max);
    for (size_t i = 0; i < result_count; ++i) {
        snprintf(names[i], SC_GAME_PROFILE_NAME_MAX, "%s",
                 candidates[i].value);
    }
    free(candidates);
    return result_count;
}
