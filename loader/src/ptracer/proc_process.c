#include "proc_process.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

ssize_t onyx_proc_read_exe(pid_t pid, char *buffer, size_t capacity) {
    if (pid <= 0 || buffer == NULL || capacity < 2) {
        errno = EINVAL;
        return -1;
    }

    char proc_path[64];
    const int path_length = snprintf(proc_path, sizeof(proc_path), "/proc/%ld/exe",
                                     (long)pid);
    if (path_length < 0 || (size_t)path_length >= sizeof(proc_path)) {
        errno = EINVAL;
        return -1;
    }

    const ssize_t length = readlink(proc_path, buffer, capacity - 1);
    if (length < 0) return -1;
    if ((size_t)length >= capacity - 1) {
        buffer[0] = '\0';
        errno = ENAMETOOLONG;
        return -1;
    }

    buffer[length] = '\0';
    return length;
}

pid_t onyx_waitpid_retry(pid_t pid, int *status, int options) {
    pid_t result;
    do {
        result = waitpid(pid, status, options);
    } while (result == -1 && errno == EINTR);
    return result;
}
