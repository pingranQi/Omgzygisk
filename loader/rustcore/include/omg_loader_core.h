#ifndef OMGZYGISK_LOADER_CORE_H
#define OMGZYGISK_LOADER_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Rust-owned stream I/O and native-endian IPC primitives. */\nint32_t omg_loader_core_connect_unix(const char *path, uint8_t retries);
ssize_t omg_loader_core_read_exact(int fd, uint8_t *buffer, size_t length);
size_t omg_loader_core_write_exact(int fd, const uint8_t *buffer, size_t length);
uint8_t omg_loader_core_read_u8(int fd);
uint32_t omg_loader_core_read_u32(int fd);
size_t omg_loader_core_read_usize(int fd);
bool omg_loader_core_write_u8(int fd, uint8_t value);
bool omg_loader_core_write_u32(int fd, uint32_t value);
bool omg_loader_core_write_usize(int fd, size_t value);

/* Must match the ABI version reported by the linked Rust static library. */
uint32_t omg_loader_core_abi_version(void);

/*
 * Apply a monitor lifecycle event.
 * States: tracing=1, stopping=2, stopped=3, exiting=4.
 * Events: start=1, stop=2, detached=3, exit=4.
 * Returns 0 for an unknown state or event.
 */
uint8_t omg_loader_core_monitor_transition(uint8_t state, uint8_t event);

/*
 * Update the zygote crash-loop counter. Returns true when retry_count is
 * reached. The caller owns both writable counter pointers and supplies
 * monotonic seconds. Invalid pointers or policy values return false.
 */
bool omg_loader_core_crash_loop_update(uint32_t *crash_count,
                                       int64_t *last_restart_seconds,
                                       int64_t now_seconds,
                                       bool was_running,
                                       int64_t window_seconds,
                                       uint32_t retry_count);

/* Validate bounded ELF64 little-endian AArch64 ET_DYN metadata. */
bool omg_loader_core_validate_elf64_aarch64(const uint8_t *data, size_t length);

/* Returns false for unknown daemon protocol action values. */
bool omg_loader_core_validate_daemon_action(uint8_t action);

/*
 * Validate one monitor control datagram.
 * 0 = valid, 1 = null pointer, 2 = oversized, 3 = malformed/unsupported.
 * The buffer remains owned by the caller and is never retained.
 */
int32_t omg_loader_core_validate_control_packet(const uint8_t *data,
                                                size_t length);

#ifdef __cplusplus
}
#endif

#endif /* OMGZYGISK_LOADER_CORE_H */
