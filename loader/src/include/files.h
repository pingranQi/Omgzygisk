#ifndef ONYX_FILES_H
#define ONYX_FILES_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*onyx_line_callback)(const char *line, size_t length, void *context);

/* Read lines until EOF, an I/O error, or a callback returns zero.
 * When trim is nonzero, strip trailing spaces/CR/LF and leading spaces.
 */
void onyx_file_readline(FILE *stream, int trim, onyx_line_callback callback, void *context);

#ifdef __cplusplus
}
#endif

#endif
