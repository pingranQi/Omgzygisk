#ifndef ONYX_PROC_PROCESS_H
#define ONYX_PROC_PROCESS_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read /proc/<pid>/exe into buffer and NUL-terminate it.
 * Returns the path length, or -1 with errno set on failure.
 * A path that does not fit is rejected rather than silently truncated.
 */
ssize_t onyx_proc_read_exe(pid_t pid, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
