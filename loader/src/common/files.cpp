#include "files.hpp"

#include <sys/sysmacros.h>

namespace {
struct LineCallbackContext {
    const std::function<bool(std::string_view)> *callback;
};

int invoke_line_callback(const char *line, size_t length, void *opaque) {
    auto *context = static_cast<LineCallbackContext *>(opaque);
    return (*context->callback)(std::string_view(line, length)) ? 1 : 0;
}
}  // namespace

void file_readline(bool trim, FILE *fp, const std::function<bool(std::string_view)> &fn) {
    LineCallbackContext context{&fn};
    onyx_file_readline(fp, trim ? 1 : 0, invoke_line_callback, &context);
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
