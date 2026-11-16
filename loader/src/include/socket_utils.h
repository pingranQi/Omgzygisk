#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t omg_socket_xread(int fd, void *buffer, size_t count);
size_t omg_socket_xwrite(int fd, const void *buffer, size_t count);
int omg_socket_read_u8(int fd, uint8_t *value);
int omg_socket_read_u32(int fd, uint32_t *value);
int omg_socket_read_usize(int fd, size_t *value);
int omg_socket_write_u8(int fd, uint8_t value);
int omg_socket_write_u32(int fd, uint32_t value);
int omg_socket_write_usize(int fd, size_t value);
char *omg_socket_read_string(int fd);
int omg_socket_write_string(int fd, const char *data, size_t length);
int omg_socket_recv_fd(int socket_fd);

#ifdef __cplusplus
}
#endif
