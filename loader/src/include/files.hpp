#pragma once

#include <dirent.h>
#include <stdio.h>

#include <memory>
#include <string>
#include <string_view>

#include "files_c.h"

using sFILE = std::unique_ptr<FILE, decltype(&fclose)>;
using sDIR = std::unique_ptr<DIR, decltype(&closedir)>;

inline sDIR make_dir(DIR *dp) {
    return sDIR(dp, [](DIR *dir) { return dir ? closedir(dir) : 1; });
}
inline sFILE make_file(FILE *fp) {
    return sFILE(fp, [](FILE *file) { return file ? fclose(file) : 1; });
}

static inline sDIR open_dir(const char *path) { return make_dir(opendir(path)); }
static inline sDIR xopen_dir(const char *path) { return make_dir(opendir(path)); }
static inline sDIR xopen_dir(int dirfd) { return make_dir(fdopendir(dirfd)); }
static inline sFILE open_file(const char *path, const char *mode) {
    return make_file(fopen(path, mode));
}
static inline sFILE xopen_file(const char *path, const char *mode) {
    return make_file(fopen(path, mode));
}
static inline sFILE xopen_file(int fd, const char *mode) { return make_file(fdopen(fd, mode)); }

/* Header-only typed bridge: no std::function allocation or out-of-line C++ TU. */
template <typename Callback>
inline void file_readline(bool trim, FILE *fp, const Callback &fn) {
    const auto bridge = [](const char *line, size_t length, const void *context) -> int {
        return (*static_cast<const Callback *>(context))(std::string_view(line, length));
    };
    omg_file_readline(fp, trim ? 1 : 0, bridge, &fn);
}

template <typename Callback>
inline void file_readline(bool trim, const char *file, const Callback &fn) {
    if (auto fp = open_file(file, "re")) file_readline(trim, fp.get(), fn);
}

template <typename Callback>
inline void file_readline(const char *file, const Callback &fn) {
    file_readline(false, file, fn);
}
