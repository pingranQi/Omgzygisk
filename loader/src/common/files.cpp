#include "files.hpp"

#include "files_c.h"

#include <sys/sysmacros.h>

namespace {
int file_line_callback(const char *line, size_t length, const void *context) {
    const auto &fn = *static_cast<const std::function<bool(std::string_view)> *>(context);
    return fn(std::string_view(line, length));
}
}  // namespace

void file_readline(bool trim, FILE *fp, const std::function<bool(std::string_view)> &fn) {
    omg_file_readline(fp, trim, file_line_callback, &fn);
}

void file_readline(bool trim, const char *file, const std::function<bool(std::string_view)> &fn) {
    if (auto fp = open_file(file, "re")) file_readline(trim, fp.get(), fn);
}
void file_readline(const char *file, const std::function<bool(std::string_view)> &fn) {
    file_readline(false, file, fn);
}

sDIR make_dir(DIR *dp) {
    return sDIR(dp, [](DIR *dp) { return dp ? closedir(dp) : 1; });
}

sFILE make_file(FILE *fp) {
    return sFILE(fp, [](FILE *fp) { return fp ? fclose(fp) : 1; });
}
