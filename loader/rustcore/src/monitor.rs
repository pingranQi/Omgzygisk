//! Platform-independent monitor lifecycle state machine.
//!
//! Timekeeping, syscalls, and ptrace status decoding stay in the platform
//! adapter. This module only owns legal state transitions and crash accounting.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TracingState {
    Tracing,
    Stopping,
    Stopped,
    Exiting,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CrashPolicy {
    pub window_seconds: i64,
    pub retry_count: u32,
}

impl Default for CrashPolicy {
    fn default() -> Self {
        Self {
            window_seconds: 30,
            retry_count: 5,
        }
    }
}

#[derive(Debug)]
pub struct Monitor<'a> {
    pub state: TracingState,
    pub crash_count: u32,
    pub last_restart_seconds: Option<i64>,
    pub policy: CrashPolicy,
    pub stop_reason: Option<&'a str>,
}

impl<'a> Default for Monitor<'a> {
    fn default() -> Self {
        Self::new(CrashPolicy::default())
    }
}

impl<'a> Monitor<'a> {
    pub fn new(policy: CrashPolicy) -> Self {
        Self {
            state: TracingState::Tracing,
            crash_count: 0,
            last_restart_seconds: None,
            policy,
            stop_reason: None,
        }
    }

    /// Record a restart only when the monitored process was running.
    /// Returns true when the configured retry threshold has been reached.
    pub fn record_zygote_restart(&mut self, now_seconds: i64, was_running: bool) -> bool {
        if !was_running {
            return false;
        }

        let outside_window = self
            .last_restart_seconds
            .and_then(|last| now_seconds.checked_sub(last))
            .map_or(true, |elapsed| elapsed < 0 || elapsed >= self.policy.window_seconds);

        if outside_window {
            self.crash_count = 1;
        } else {
            self.crash_count = self.crash_count.saturating_add(1);
        }

        self.last_restart_seconds = Some(now_seconds);
        self.crash_count >= self.policy.retry_count
    }

    pub fn notify_injected(&mut self) {
        self.crash_count = 0;
        self.last_restart_seconds = None;
    }

    pub fn request_stop(&mut self, reason: &'a str) {
        if self.state == TracingState::Tracing {
            self.state = TracingState::Stopping;
            self.stop_reason = Some(reason);
        }
    }

    pub fn notify_detached(&mut self) {
        if self.state == TracingState::Stopping {
            self.state = TracingState::Stopped;
        }
    }

    pub fn request_start(&mut self) {
        if matches!(self.state, TracingState::Stopping | TracingState::Stopped) {
            self.state = TracingState::Tracing;
            self.stop_reason = None;
        }
    }

    pub fn request_exit(&mut self) {
        self.state = TracingState::Exiting;
        self.stop_reason = Some("user requested");
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn counts_only_running_zygote_restarts() {
        let mut monitor = Monitor::new(CrashPolicy {
            window_seconds: 30,
            retry_count: 3,
        });
        assert!(!monitor.record_zygote_restart(1, false));
        assert!(!monitor.record_zygote_restart(1, true));
        assert!(!monitor.record_zygote_restart(10, true));
        assert!(monitor.record_zygote_restart(20, true));
        assert_eq!(monitor.crash_count, 3);
    }

    #[test]
    fn successful_injection_resets_crash_history() {
        let mut monitor = Monitor::new(CrashPolicy {
            retry_count: 2,
            ..CrashPolicy::default()
        });
        let _ = monitor.record_zygote_restart(1, true);
        monitor.notify_injected();
        assert_eq!(monitor.crash_count, 0);
        assert!(!monitor.record_zygote_restart(2, true));
    }

    #[test]
    fn backwards_clock_movement_starts_a_fresh_crash_window() {
        let mut monitor = Monitor::new(CrashPolicy {
            window_seconds: 30,
            retry_count: 2,
        });
        let _ = monitor.record_zygote_restart(100, true);
        assert!(!monitor.record_zygote_restart(90, true));
        assert_eq!(monitor.crash_count, 1);
    }

    #[test]
    fn stop_and_exit_transitions_are_deterministic() {
        let mut monitor = Monitor::default();
        monitor.request_stop("daemon failed");
        assert_eq!(monitor.state, TracingState::Stopping);
        monitor.notify_detached();
        assert_eq!(monitor.state, TracingState::Stopped);
        monitor.request_start();
        assert_eq!(monitor.state, TracingState::Tracing);
        monitor.request_exit();
        assert_eq!(monitor.state, TracingState::Exiting);
        assert_eq!(monitor.stop_reason, Some("user requested"));
    }
}
