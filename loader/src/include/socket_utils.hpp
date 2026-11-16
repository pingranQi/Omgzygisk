#pragma once

#include "socket_utils.h"

#include <cstdlib>
#include <string>
#include <string_view>

namespace socket_utils {

inline ssize_t xread(int fd, void *buffer, size_t count) {
    return omg_socket_xread(fd, buffer, count);
}

inline size_t xwrite(int fd, const void *buffer, size_t count) {
    return omg_socket_xwrite(fd, buffer, count);
}

inline uint8_t read_u8(int fd) { return omg_socket_read_u8(fd); }
inline uint32_t read_u32(int fd) { return omg_socket_read_u32(fd); }
inline size_t read_usize(int fd) { return omg_socket_read_usize(fd); }

inline std::string read_string(int fd) {
    char *value = omg_socket_read_string(fd);
    if (!value) return {};
    std::string result(value);
    std::free(value);
    return result;
}

inline bool write_u8(int fd, uint8_t value) {
    return omg_socket_write_u8(fd, value) != 0;
}
inline bool write_u32(int fd, uint32_t value) {
    return omg_socket_write_u32(fd, value) != 0;
}
inline int recv_fd(int fd) { return omg_socket_recv_fd(fd); }
inline bool write_usize(int fd, size_t value) {
    return omg_socket_write_usize(fd, value) != 0;
}
inline bool write_string(int fd, std::string_view value) {
    return omg_socket_write_string(fd, value.data(), value.size()) != 0;
}

}  // namespace socket_utils
