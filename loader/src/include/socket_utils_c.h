#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t omg_socket_read_exact(int fd, void *buffer, size_t count);
size_t omg_socket_write_exact(int fd, const void *buffer, size_t count);
uint8_t omg_socket_read_u8(int fd);
uint32_t omg_socket_read_u32(int fd);
size_t omg_socket_read_usize(int fd);
int omg_socket_write_u8(int fd, uint8_t value);
int omg_socket_write_u32(int fd, uint32_t value);
int omg_socket_write_usize(int fd, size_t value);
int omg_socket_write_string(int fd, const char *value, size_t length);
int omg_socket_recv_fd(int fd);

#ifdef __cplusplus
}
#endif
