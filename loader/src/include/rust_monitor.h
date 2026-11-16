#ifndef OMG_RUST_MONITOR_H
#define OMG_RUST_MONITOR_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct OmgMonitorHandle OmgMonitorHandle;
OmgMonitorHandle *omg_monitor_create(void);
void omg_monitor_destroy(OmgMonitorHandle *handle);
uint32_t omg_monitor_state(const OmgMonitorHandle *handle);
bool omg_monitor_set_state(OmgMonitorHandle *handle, uint32_t state);
void omg_monitor_request_start(OmgMonitorHandle *handle);
void omg_monitor_request_stop(OmgMonitorHandle *handle);
void omg_monitor_request_exit(OmgMonitorHandle *handle);
void omg_monitor_notify_detached(OmgMonitorHandle *handle);
#ifdef __cplusplus
}
#endif
#endif
