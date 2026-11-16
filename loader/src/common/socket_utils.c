#include "socket_utils_c.h"

#include <errno.h>
#include <android/log.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

#include "omg_loader_core.h"

#define LOG_TAG "zygisk-socket"

static void log_error(const char *message, long actual, size_t expected) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s (%ld != %zu)", message,
                        actual, expected);
}

ssize_t omg_socket_read_exact(int fd, void *buffer, size_t count) {
    const ssize_t result = omg_loader_core_read_exact(fd, (uint8_t *)buffer, count);
    if (result < 0 || (size_t)result != count) {
        log_error("read", (long)result, count);
    }
    return result;
}

size_t omg_socket_write_exact(int fd, const void *buffer, size_t count) {
    const size_t result = omg_loader_core_write_exact(fd, (const uint8_t *)buffer, count);
    if (result != count) log_error("write", (long)result, count);
    return result;
}

uint8_t omg_socket_read_u8(int fd) {
    return omg_loader_core_read_u8(fd);
}

uint32_t omg_socket_read_u32(int fd) {
    return omg_loader_core_read_u32(fd);
}

size_t omg_socket_read_usize(int fd) {
    return omg_loader_core_read_usize(fd);
}

int omg_socket_write_u8(int fd, uint8_t value) {
    return omg_loader_core_write_u8(fd, value);
}

int omg_socket_write_u32(int fd, uint32_t value) {
    return omg_loader_core_write_u32(fd, value);
}

int omg_socket_write_usize(int fd, size_t value) {
    return omg_loader_core_write_usize(fd, value);
}

int omg_socket_write_string(int fd, const char *value, size_t length) {
    if (value == NULL && length != 0) return 0;
    if (!omg_socket_write_usize(fd, length)) return 0;
    return length == 0 || omg_socket_write_exact(fd, value, length) == length;
}

/* Receive exactly one SCM_RIGHTS descriptor and close every rejected descriptor. */
int omg_socket_recv_fd(int sockfd) {
    int payload = 0;
    struct iovec iov = { .iov_base = &payload, .iov_len = sizeof(payload) };
    union {
        struct cmsghdr align;
        char bytes[CMSG_SPACE(sizeof(int))];
    } control;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    ssize_t received;

    memset(&msg, 0, sizeof(msg));
    memset(&control, 0, sizeof(control));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.bytes;
    msg.msg_controllen = sizeof(control.bytes);

    do {
        received = recvmsg(sockfd, &msg, MSG_CMSG_CLOEXEC);
    } while (received < 0 && errno == EINTR);

    cmsg = CMSG_FIRSTHDR(&msg);
    if (received == (ssize_t)sizeof(payload) && !(msg.msg_flags & MSG_CTRUNC) &&
        cmsg != NULL && cmsg->cmsg_level == SOL_SOCKET &&
        cmsg->cmsg_type == SCM_RIGHTS && cmsg->cmsg_len == CMSG_LEN(sizeof(int)) &&
        CMSG_NXTHDR(&msg, cmsg) == NULL) {
        memcpy(&payload, CMSG_DATA(cmsg), sizeof(payload));
        return payload;
    }

    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(0)) {
            const size_t bytes = cmsg->cmsg_len - CMSG_LEN(0);
            const size_t count = bytes / sizeof(int);
            int *fds = (int *)CMSG_DATA(cmsg);
            size_t i;
            for (i = 0; i < count; ++i) close(fds[i]);
        }
    }
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "recv_fd: rejected malformed descriptor message (result=%ld)",
                        (long)received);
    return -1;
}
