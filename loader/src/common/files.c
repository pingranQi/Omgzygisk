#include "files_c.h"

#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

void omg_file_readline(FILE *stream, int trim, omg_file_line_callback callback, const void *context) {
    if (stream == NULL || callback == NULL) {
        return;
    }

    char *buffer = NULL;
    size_t capacity = 0;
    ssize_t length;

    while ((length = getline(&buffer, &capacity, stream)) >= 0) {
        size_t start = 0;
        size_t line_length = (size_t)length;

        if (trim) {
            while (line_length > 0) {
                const char ch = buffer[line_length - 1];
                if (ch != '\n' && ch != '\r' && ch != ' ') {
                    break;
                }
                --line_length;
            }
            while (start < line_length && buffer[start] == ' ') {
                ++start;
            }
        }

        if (!callback(buffer + start, line_length - start, context)) {
            break;
        }
    }

    free(buffer);
}
