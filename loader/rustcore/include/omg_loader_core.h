#ifndef OMGZYGISK_LOADER_CORE_H
#define OMGZYGISK_LOADER_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Must match the ABI version reported by the linked Rust static library. */
uint32_t omg_loader_core_abi_version(void);

/*
 * Apply a monitor lifecycle event.
 * States: tracing=1, stopping=2, stopped=3, exiting=4.
 * Events: start=1, stop=2, detached=3, exit=4.
 * Returns 0 for an unknown state or event.
 */
uint8_t omg_loader_core_monitor_transition(uint8_t state, uint8_t event);

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
