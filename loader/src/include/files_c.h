#pragma once

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*omg_file_line_callback)(const char *line, size_t length, const void *context);

/* Reads lines without retaining buffers or allocating until getline needs them. */
void omg_file_readline(FILE *stream, int trim, omg_file_line_callback callback, const void *context);

#ifdef __cplusplus
}
#endif
