#include "files.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

void onyx_file_readline(FILE *stream, int trim, onyx_line_callback callback, void *context) {
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    if (stream == NULL || callback == NULL) {
        return;
    }

    while ((length = getline(&line, &capacity, stream)) >= 0) {
        size_t begin = 0;
        size_t end = (size_t)length;

        if (trim) {
            while (end > begin &&
                   (line[end - 1] == '\n' || line[end - 1] == '\r' || line[end - 1] == ' ')) {
                --end;
            }
            while (begin < end && line[begin] == ' ') {
                ++begin;
            }
        }

        if (!callback(line + begin, end - begin, context)) {
            break;
        }
    }

    free(line);
}
