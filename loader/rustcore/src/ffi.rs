//! Narrow C ABI for consumers that need to call the Rust loader core.
//!
//! Keep this interface primitive-only: no Rust-owned structs, strings, or
//! allocator state cross the boundary.

use crate::control::{self, Command};

const MAX_CONTROL_PACKET_BYTES: usize = 8 + control::MAX_PAYLOAD_BYTES;

/// Apply one monitor lifecycle event. State values: tracing=1, stopping=2,
/// stopped=3, exiting=4. Event values: start=1, stop=2, detached=3, exit=4.
/// Returns 0 for unknown state/event values.
#[unsafe(no_mangle)]
pub extern "C" fn omg_loader_core_monitor_transition(state: u8, event: u8) -> u8 {
    crate::monitor::transition(state, event)
}


/// Update the zygote crash-loop counter using monotonic seconds.
/// Returns false for invalid pointers/policy values. A non-running prior
/// generation does not increment the counter.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_crash_loop_update(
    crash_count: *mut u32,
    last_restart_seconds: *mut i64,
    now_seconds: i64,
    was_running: bool,
    window_seconds: i64,
    retry_count: u32,
) -> bool {
    if crash_count.is_null()
        || last_restart_seconds.is_null()
        || window_seconds <= 0
        || retry_count == 0
    {
        return false;
    }
    if !was_running {
        return false;
    }

    // SAFETY: pointers are checked above; caller guarantees writable storage.
    let count = unsafe { &mut *crash_count };
    let last = unsafe { &mut *last_restart_seconds };
    let elapsed = now_seconds.checked_sub(*last);
    if elapsed.map_or(true, |value| value < 0 || value >= window_seconds) {
        *count = 1;
    } else {
        *count = count.saturating_add(1);
    }
    *last = now_seconds;
    *count >= retry_count
}

/// ABI version for compile-time/runtime compatibility checks.
#[unsafe(no_mangle)]
pub extern "C" fn omg_loader_core_abi_version() -> u32 {
    1
}

/// Returns true when action is a daemon socket action understood by this
/// version of the loader core. Unknown values must be rejected, never guessed.
#[unsafe(no_mangle)]
pub extern "C" fn omg_loader_core_validate_daemon_action(action: u8) -> bool {
    crate::ipc::DaemonSocketAction::try_from(action).is_ok()
}

/// Validate a monitor control datagram.
///
/// Return values:
/// - 0: valid packet
/// - 1: null pointer with non-zero length
/// - 2: packet exceeds the configured maximum
/// - 3: malformed or unsupported packet
///
/// The caller retains ownership of data; the function never stores it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_validate_control_packet(
    data: *const u8,
    length: usize,
) -> i32 {
    if length > MAX_CONTROL_PACKET_BYTES {
        return 2;
    }
    if data.is_null() {
        return if length == 0 { 3 } else { 1 };
    }

    // SAFETY: the C caller promises that data points to length readable bytes
    // for the duration of this call. Null and maximum length are checked
    // above before constructing the borrowed slice.
    let bytes = unsafe { std::slice::from_raw_parts(data, length) };
    match control::decode(bytes) {
        Ok(packet) if matches!(packet.command, Command::Start
            | Command::Stop
            | Command::Exit
            | Command::ZygoteInjected
            | Command::DaemonSetInfo
            | Command::DaemonSetErrorInfo
            | Command::SystemServerStarted) => 0,
        _ => 3,
    }
}

#[cfg(test)]
mod tests {
    use super::*;


    #[test]
    fn crash_loop_counter_matches_window_and_running_generation_policy() {
        let mut count = 0;
        let mut last = 0;
        assert!(!unsafe { omg_loader_core_crash_loop_update(&mut count, &mut last, 100, false, 30, 3) });
        assert_eq!((count, last), (0, 0));
        assert!(!unsafe { omg_loader_core_crash_loop_update(&mut count, &mut last, 100, true, 30, 3) });
        assert!(!unsafe { omg_loader_core_crash_loop_update(&mut count, &mut last, 110, true, 30, 3) });
        assert!(unsafe { omg_loader_core_crash_loop_update(&mut count, &mut last, 120, true, 30, 3) });
        assert_eq!((count, last), (3, 120));
        assert!(!unsafe { omg_loader_core_crash_loop_update(&mut count, &mut last, 90, true, 30, 3) });
        assert_eq!(count, 1);
    }

    #[test]
    fn crash_loop_counter_rejects_invalid_arguments() {
        let mut count = 0;
        let mut last = 0;
        assert!(!unsafe { omg_loader_core_crash_loop_update(std::ptr::null_mut(), &mut last, 1, true, 30, 3) });
        assert!(!unsafe { omg_loader_core_crash_loop_update(&mut count, &mut last, 1, true, 0, 3) });
        assert!(!unsafe { omg_loader_core_crash_loop_update(&mut count, &mut last, 1, true, 30, 0) });
    }

    #[test]
    fn exposes_monitor_lifecycle_transitions() {
        assert_eq!(omg_loader_core_monitor_transition(1, 2), 2);
        assert_eq!(omg_loader_core_monitor_transition(2, 3), 3);
        assert_eq!(omg_loader_core_monitor_transition(3, 1), 1);
        assert_eq!(omg_loader_core_monitor_transition(4, 1), 4);
        assert_eq!(omg_loader_core_monitor_transition(0, 1), 0);
    }

    #[test]
    fn exports_stable_abi_version() {
        assert_eq!(omg_loader_core_abi_version(), 1);
    }

    #[test]
    fn validates_only_known_daemon_actions() {
        assert!(omg_loader_core_validate_daemon_action(0));
        assert!(omg_loader_core_validate_daemon_action(13));
        assert!(!omg_loader_core_validate_daemon_action(14));
        assert!(!omg_loader_core_validate_daemon_action(u8::MAX));
    }

    #[test]
    fn rejects_packets_larger_than_the_wire_limit() {
        // SAFETY: length is rejected before the pointer is dereferenced.
        assert_eq!(
            unsafe { omg_loader_core_validate_control_packet(std::ptr::null(), MAX_CONTROL_PACKET_BYTES + 1) },
            2
        );
    }

    #[test]
    fn validates_control_packet_without_taking_ownership() {
        let packet = [1u8, 0, 0, 0];
        // SAFETY: packet is a live array and its exact readable length is used.
        assert_eq!(
            unsafe { omg_loader_core_validate_control_packet(packet.as_ptr(), packet.len()) },
            0
        );
        // SAFETY: zero length is accepted with a non-null pointer.
        assert_eq!(
            unsafe { omg_loader_core_validate_control_packet(packet.as_ptr(), 0) },
            3
        );
        // SAFETY: null is handled before any slice is constructed.
        assert_eq!(unsafe { omg_loader_core_validate_control_packet(std::ptr::null(), 1) }, 1);
    }
}
