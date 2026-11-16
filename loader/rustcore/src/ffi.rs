//! Narrow C ABI for consumers that need to call the Rust loader core.
//!
//! Keep this interface primitive-only: no Rust-owned structs, strings, or
//! allocator state cross the boundary.

use crate::control::{self, Command};

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
    if length > control::MAX_PAYLOAD_BYTES {
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
