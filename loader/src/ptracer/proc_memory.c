#include "proc_memory.h"

#include <errno.h>
#include <sys/uio.h>

ssize_t onyx_proc_read_memory(pid_t pid, uintptr_t remote_address, void *buffer, size_t length) {
    if (pid <= 0 || (buffer == NULL && length != 0)) {
        errno = EINVAL;
        return -1;
    }

    struct iovec local = {.iov_base = buffer, .iov_len = length};
    struct iovec remote = {.iov_base = (void *)remote_address, .iov_len = length};
    return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

ssize_t onyx_proc_write_memory(pid_t pid, uintptr_t remote_address, const void *buffer,
                               size_t length) {
    if (pid <= 0 || (buffer == NULL && length != 0)) {
        errno = EINVAL;
        return -1;
    }

    struct iovec local = {.iov_base = (void *)buffer, .iov_len = length};
    struct iovec remote = {.iov_base = (void *)remote_address, .iov_len = length};
    return process_vm_writev(pid, &local, 1, &remote, 1, 0);
}
