#ifndef ONYX_PARSE_INT_H
#define ONYX_PARSE_INT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse a complete decimal integer from a bounded byte range. */
int onyx_parse_int(const char *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
