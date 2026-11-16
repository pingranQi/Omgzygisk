#include "socket_utils.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

ssize_t omg_socket_xread(int fd, void *buffer, size_t count) {
    size_t completed = 0;
    while (completed < count) {
        ssize_t result = read(fd, (uint8_t *)buffer + completed, count - completed);
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (result == 0) break;
        completed += (size_t)result;
    }
    return (ssize_t)completed;
}

size_t omg_socket_xwrite(int fd, const void *buffer, size_t count) {
    size_t completed = 0;
    while (completed < count) {
        ssize_t result = write(fd, (const uint8_t *)buffer + completed, count - completed);
        if (result < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (result == 0) break;
        completed += (size_t)result;
    }
    return completed;
}

static int read_exact(int fd, void *value, size_t size) {
    return omg_socket_xread(fd, value, size) == (ssize_t)size;
}

static int write_exact(int fd, const void *value, size_t size) {
    return omg_socket_xwrite(fd, value, size) == size;
}

uint8_t omg_socket_read_u8(int fd) {
    uint8_t value = 0;
    (void)read_exact(fd, &value, sizeof(value));
    return value;
}

uint32_t omg_socket_read_u32(int fd) {
    uint32_t value = 0;
    (void)read_exact(fd, &value, sizeof(value));
    return value;
}

size_t omg_socket_read_usize(int fd) {
    size_t value = 0;
    (void)read_exact(fd, &value, sizeof(value));
    return value;
}

char *omg_socket_read_string(int fd) {
    size_t length = omg_socket_read_usize(fd);
    if (length == SIZE_MAX) return NULL;
    char *value = (char *)malloc(length + 1);
    if (!value) return NULL;
    if (!read_exact(fd, value, length)) {
        free(value);
        return NULL;
    }
    value[length] = '\0';
    return value;
}

int omg_socket_write_u8(int fd, uint8_t value) {
    return write_exact(fd, &value, sizeof(value));
}

int omg_socket_write_u32(int fd, uint32_t value) {
    return write_exact(fd, &value, sizeof(value));
}

int omg_socket_write_usize(int fd, size_t value) {
    return write_exact(fd, &value, sizeof(value));
}

int omg_socket_write_string(int fd, const char *value, size_t length) {
    return omg_socket_write_usize(fd, length) &&
           omg_socket_xwrite(fd, value, length) == length;
}

int omg_socket_recv_fd(int sockfd) {
    int payload = 0;
    char control[CMSG_SPACE(sizeof(int))];
    struct iovec iov = {
        .iov_base = &payload,
        .iov_len = sizeof(payload),
    };
    struct msghdr message = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = control,
        .msg_controllen = sizeof(control),
        .msg_flags = 0,
    };

    ssize_t received;
    do {
        received = recvmsg(sockfd, &message, MSG_WAITALL);
    } while (received < 0 && errno == EINTR);

    if (received != (ssize_t)sizeof(payload) ||
        (message.msg_flags & MSG_CTRUNC) != 0) {
        return -1;
    }

    struct cmsghdr *header = CMSG_FIRSTHDR(&message);
    if (!header || header->cmsg_level != SOL_SOCKET ||
        header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len < CMSG_LEN(sizeof(int))) {
        return -1;
    }

    int received_fd = -1;
    memcpy(&received_fd, CMSG_DATA(header), sizeof(received_fd));
    return received_fd;
}
