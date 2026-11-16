#pragma once

#include <string>
#include <string_view>
#include <sys/socket.h>

#include "socket_utils_c.h"

namespace socket_utils {
inline ssize_t xread(int fd, void *buf, size_t count) {
    return omg_socket_read_exact(fd, buf, count);
}
inline size_t xwrite(int fd, const void *buf, size_t count) {
    return omg_socket_write_exact(fd, buf, count);
}
inline uint8_t read_u8(int fd) { return omg_socket_read_u8(fd); }
inline uint32_t read_u32(int fd) { return omg_socket_read_u32(fd); }
inline size_t read_usize(int fd) { return omg_socket_read_usize(fd); }
inline bool write_u8(int fd, uint8_t value) { return omg_socket_write_u8(fd, value) != 0; }
inline bool write_u32(int fd, uint32_t value) { return omg_socket_write_u32(fd, value) != 0; }
inline bool write_usize(int fd, size_t value) { return omg_socket_write_usize(fd, value) != 0; }
inline int recv_fd(int fd) { return omg_socket_recv_fd(fd); }
inline std::string read_string(int fd) {
    constexpr size_t max_length = 1024u * 1024u;
    const size_t length = read_usize(fd);
    if (length > max_length) {
        shutdown(fd, SHUT_RDWR);
        return {};
    }
    std::string value(length, '\\0');
    if (length != 0 && xread(fd, value.data(), length) != static_cast<ssize_t>(length)) {
        shutdown(fd, SHUT_RDWR);
        return {};
    }
    return value;
}
inline bool write_string(int fd, std::string_view value) {
    return omg_socket_write_string(fd, value.data(), value.size()) != 0;
}
}
