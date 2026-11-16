#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Clear stale TracerPid state on kernels where ptrace detach can leave it
 * visible. This helper is a no-op when seccomp filter installation is
 * detectable through /proc/self/status.
 */
void perform_ptrace_message_clear(void);

#ifdef __cplusplus
}
#endif
