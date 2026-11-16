/*
 * Small, allocation-conscious Unix socket protocol primitives.
 * The implementation is C so the native transport layer does not pull in
 * the C++ runtime. C++ string conveniences live in socket_utils.hpp.
 */
#include "socket_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

ssize_t omg_socket_xread(int fd, void *buffer, size_t count) {
    size_t offset = 0;
    while (offset < count) {
        ssize_t result = read(fd, (unsigned char *)buffer + offset, count - offset);
        if (result < 0) {
            if (errno == EINTR) continue;
            return result;
        }
        if (result == 0) break;
        offset += (size_t)result;
    }
    return (ssize_t)offset;
}

size_t omg_socket_xwrite(int fd, const void *buffer, size_t count) {
    size_t offset = 0;
    while (offset < count) {
        ssize_t result = write(fd, (const unsigned char *)buffer + offset, count - offset);
        if (result < 0) {
            if (errno == EINTR) continue;
            return offset;
        }
        if (result == 0) break;
        offset += (size_t)result;
    }
    return offset;
}

int omg_socket_read_u8(int fd, uint8_t *value) {
    return omg_socket_xread(fd, value, sizeof(*value)) == (ssize_t)sizeof(*value);
}

int omg_socket_read_u32(int fd, uint32_t *value) {
    return omg_socket_xread(fd, value, sizeof(*value)) == (ssize_t)sizeof(*value);
}

int omg_socket_read_usize(int fd, size_t *value) {
    return omg_socket_xread(fd, value, sizeof(*value)) == (ssize_t)sizeof(*value);
}

int omg_socket_write_u8(int fd, uint8_t value) {
    return omg_socket_xwrite(fd, &value, sizeof(value)) == sizeof(value);
}

int omg_socket_write_u32(int fd, uint32_t value) {
    return omg_socket_xwrite(fd, &value, sizeof(value)) == sizeof(value);
}

int omg_socket_write_usize(int fd, size_t value) {
    return omg_socket_xwrite(fd, &value, sizeof(value)) == sizeof(value);
}

char *omg_socket_read_string(int fd) {
    size_t length = 0;
    if (!omg_socket_read_usize(fd, &length)) return NULL;
    if (length == SIZE_MAX) {
        errno = EOVERFLOW;
        return NULL;
    }

    char *buffer = (char *)malloc(length + 1);
    if (buffer == NULL) return NULL;
    if (length != 0 && omg_socket_xread(fd, buffer, length) != (ssize_t)length) {
        free(buffer);
        return NULL;
    }
    buffer[length] = '\0';
    return buffer;
}

int omg_socket_write_string(int fd, const char *data, size_t length) {
    return omg_socket_write_usize(fd, length) &&
           omg_socket_xwrite(fd, data, length) == length;
}

int omg_socket_recv_fd(int socket_fd) {
    int dummy = 0;
    struct iovec iov = {
        .iov_base = &dummy,
        .iov_len = sizeof(dummy),
    };
    union {
        struct cmsghdr align;
        unsigned char bytes[CMSG_SPACE(sizeof(int))];
    } control;
    struct msghdr message;
    memset(&message, 0, sizeof(message));
    message.msg_iov = &iov;
    message.msg_iovlen = 1;
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);

    ssize_t received;
    do {
        received = recvmsg(socket_fd, &message, MSG_WAITALL | MSG_CMSG_CLOEXEC);
    } while (received < 0 && errno == EINTR);

    if (received != (ssize_t)sizeof(dummy) ||
        (message.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) != 0) {
        if (received >= 0) errno = EPROTO;
        return -1;
    }

    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    if (header == NULL || header->cmsg_level != SOL_SOCKET ||
        header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len < CMSG_LEN(sizeof(int))) {
        errno = EPROTO;
        return -1;
    }

    const size_t first_payload = header->cmsg_len - CMSG_LEN(0);
    const size_t first_count = first_payload / sizeof(int);
    int *first_fds = (int *)CMSG_DATA(header);
    if (first_count == 0) {
        errno = EPROTO;
        return -1;
    }

    int result = first_fds[0];
    for (size_t i = 1; i < first_count; ++i) close(first_fds[i]);

    /* Close descriptors from any unexpected additional control messages. */
    for (struct cmsghdr *next = CMSG_NXTHDR(&message, header);
         next != NULL; next = CMSG_NXTHDR(&message, next)) {
        if (next->cmsg_level == SOL_SOCKET && next->cmsg_type == SCM_RIGHTS &&
            next->cmsg_len >= CMSG_LEN(sizeof(int))) {
            const size_t payload = next->cmsg_len - CMSG_LEN(0);
            const size_t count = payload / sizeof(int);
            int *fds = (int *)CMSG_DATA(next);
            for (size_t i = 0; i < count; ++i) close(fds[i]);
        }
    }
    return result;
}
