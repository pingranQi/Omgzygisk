#pragma once

#include <sys/types.h>
#include <cstdint>

#include <string>

// Defines common data structures used across the monitor application.

enum TracingState { TRACING = 1, STOPPING, STOPPED, EXITING };

struct Status {
    bool supported = false;
    bool zygote_injected = false;
    bool daemon_running = false;
    pid_t daemon_pid = -1;
    std::string daemon_info;
    std::string daemon_error_info;
};

struct StartCounter {
    int64_t last_start_seconds = 0;
    uint32_t count = 0;
};
