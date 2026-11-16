#ifndef ONYX_PROC_MEMORY_H
#define ONYX_PROC_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Transfer exactly len bytes when possible. Return the kernel-reported byte
 * count, or -1 with errno set on failure. Partial transfers are not hidden.
 */
ssize_t onyx_proc_read_memory(pid_t pid, uintptr_t remote_address, void *buffer, size_t length);
ssize_t onyx_proc_write_memory(pid_t pid, uintptr_t remote_address, const void *buffer,
                               size_t length);

#ifdef __cplusplus
}
#endif

#endif
