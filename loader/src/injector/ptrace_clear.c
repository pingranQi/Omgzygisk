/*
 * Kernel-level TracerPid cleanup via seccomp BPF.
 *
 * Adapted from PerformanC/ReZygisk. Keep this translation unit C-only so the
 * Android process adapter does not need C++ runtime support for this helper.
 */

#include "ptrace_clear.hpp"

#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef LOG_TAG
#if defined(__LP64__)
#define LOG_TAG "zygisk-core64"
#else
#define LOG_TAG "zygisk-core32"
#endif
#endif

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define PLOGE(message) \
    do { \
        const int saved_errno = errno; \
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: %s", message, strerror(saved_errno)); \
    } while (0)

static int seccomp_filters_visible(void) {
    FILE *status_file = fopen("/proc/self/status", "r");
    char line[256];

    if (status_file == NULL) {
        PLOGE("open /proc/self/status");
        return 1;
    }

    while (fgets(line, sizeof(line), status_file) != NULL) {
        if (strncmp(line, "Seccomp_filters:", 16) == 0) {
            fclose(status_file);
            return 1;
        }
    }

    fclose(status_file);
    return 0;
}

void perform_ptrace_message_clear(void) {
    int random_fd;
    uint32_t args[4] = {0};
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_exit_group, 0, 9),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 7),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 5),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 3),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[3])),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRACE),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    struct sock_fprog program;

    if (seccomp_filters_visible()) {
        LOGD("ptrace_clear: Seccomp filters are visible, skipping seccomp-based cleanup");
        return;
    }

    random_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (random_fd < 0) {
        PLOGE("ptrace_clear: open /dev/urandom");
        return;
    }

    {
        ssize_t count = read(random_fd, args, sizeof(args));
        close(random_fd);
        if (count != (ssize_t)sizeof(args)) {
            if (count >= 0) errno = EIO;
            PLOGE("ptrace_clear: read /dev/urandom");
            return;
        }
    }

    args[0] |= 0x10000U;

    /* Patch the four immediate values with this invocation's random key. */
    filter[3].k = args[0];
    filter[5].k = args[1];
    filter[7].k = args[2];
    filter[9].k = args[3];

    program.len = (unsigned short)(sizeof(filter) / sizeof(filter[0]));
    program.filter = filter;

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) != 0) {
        PLOGE("ptrace_clear: prctl(SECCOMP)");
        return;
    }

    /*
     * This exact exit_group invocation matches the filter and raises TRACE.
     * With no tracer attached, the kernel consumes the stop without executing
     * exit_group, clearing the stale tracer state on affected kernels.
     */
    syscall(__NR_exit_group, args[0], args[1], args[2], args[3]);
    LOGD("ptrace_clear: seccomp-based TracerPid cleanup done");
}
