//! Narrow C ABI for consumers that need to call the Rust loader core.
//!
//! Keep this interface primitive-only: no Rust-owned structs, strings, or
//! allocator state cross the boundary.

use crate::control::{self, Command};

unsafe extern "C" {
    fn read(fd: i32, buffer: *mut core::ffi::c_void, count: usize) -> isize;
    fn write(fd: i32, buffer: *const core::ffi::c_void, count: usize) -> isize;
    fn __errno() -> *mut i32;
}

/// Read bytes from a stream, retrying interrupted system calls.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_read_exact(fd: i32, buffer: *mut u8, length: usize) -> isize {
    if buffer.is_null() && length != 0 { return -1; }
    let mut offset = 0usize;
    while offset < length {
        // SAFETY: the caller provides a writable buffer of length bytes.
        let result = unsafe { read(fd, buffer.add(offset).cast(), length - offset) };
        if result < 0 {
            // Android bionic's __errno() exposes the calling thread's errno.
            // EINTR is 4 on Linux/Android and must preserve the old retry behavior.
            if unsafe { *__errno() } == 4 { continue; }
            return -1;
        }
        if result == 0 { break; }
        offset += result as usize;
    }
    offset as isize
}

/// Write bytes to a stream, returning the number of bytes written.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_write_exact(fd: i32, buffer: *const u8, length: usize) -> usize {
    if buffer.is_null() && length != 0 { return 0; }
    let mut offset = 0usize;
    while offset < length {
        // SAFETY: the caller provides a readable buffer of length bytes.
        let result = unsafe { write(fd, buffer.add(offset).cast(), length - offset) };
        if result < 0 {
            if unsafe { *__errno() } == 4 { continue; }
            return offset;
        }
        if result == 0 { break; }
        offset += result as usize;
    }
    offset
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_read_u8(fd: i32) -> u8 {
    let mut value = 0u8;
    // SAFETY: value is writable for one byte.
    if unsafe { omg_loader_core_read_exact(fd, &mut value, 1) } != 1 { 0 } else { value }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_read_u32(fd: i32) -> u32 {
    let mut bytes = [0u8; 4];
    // SAFETY: bytes is writable for four bytes.
    if unsafe { omg_loader_core_read_exact(fd, bytes.as_mut_ptr(), bytes.len()) } != 4 { 0 }
    else { u32::from_ne_bytes(bytes) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_read_usize(fd: i32) -> usize {
    let mut bytes = [0u8; core::mem::size_of::<usize>()];
    // SAFETY: bytes is writable for its full length.
    if unsafe { omg_loader_core_read_exact(fd, bytes.as_mut_ptr(), bytes.len()) } != bytes.len() as isize { 0 }
    else { usize::from_ne_bytes(bytes) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_write_u8(fd: i32, value: u8) -> bool {
    // SAFETY: value is readable for one byte.
    unsafe { omg_loader_core_write_exact(fd, &value, 1) == 1 }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_write_u32(fd: i32, value: u32) -> bool {
    let bytes = value.to_ne_bytes();
    // SAFETY: bytes remains alive and readable during the call.
    unsafe { omg_loader_core_write_exact(fd, bytes.as_ptr(), bytes.len()) == bytes.len() }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_write_usize(fd: i32, value: usize) -> bool {
    let bytes = value.to_ne_bytes();
    // SAFETY: bytes remains alive and readable during the call.
    unsafe { omg_loader_core_write_exact(fd, bytes.as_ptr(), bytes.len()) == bytes.len() }
}



/// Connect to a Unix-domain stream socket, retrying once per second.
/// Returns the owned file descriptor, or the negated OS error number.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_connect_unix(path: *const core::ffi::c_char, retries: u8) -> i32 {
    use std::ffi::CStr;
    use std::os::fd::IntoRawFd;
    use std::os::unix::ffi::OsStrExt;
    use std::os::unix::net::UnixStream;
    use std::time::Duration;

    if path.is_null() { return -22; } // EINVAL
    // SAFETY: the caller supplies a valid NUL-terminated path for this call.
    let path = unsafe { CStr::from_ptr(path) };
    let path = std::ffi::OsStr::from_bytes(path.to_bytes());
    let mut attempts = retries;
    let mut last_error = 2; // ENOENT, matching a missing daemon socket.
    while attempts != 0 {
        match UnixStream::connect(path) {
            Ok(stream) => return stream.into_raw_fd(),
            Err(error) => last_error = error.raw_os_error().unwrap_or(5),
        }
        attempts -= 1;
        if attempts != 0 {
            std::thread::sleep(Duration::from_secs(1));
        }
    }
    -last_error
}

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


/// Validate ELF64 little-endian AArch64 ET_DYN metadata before C++ maps sections.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_loader_core_validate_elf64_aarch64(data: *const u8, length: usize) -> bool {
    const EHDR: usize = 64;
    const PHDR: usize = 56;
    const SHDR: usize = 64;
    const SYM: usize = 24;
    if data.is_null() || length < EHDR { return false; }
    // SAFETY: caller provides length readable bytes; null and minimum size are checked.
    let b = unsafe { std::slice::from_raw_parts(data, length) };
    if &b[0..4] != b"\x7fELF" || b[4] != 2 || b[5] != 1 || b[6] != 1
        || u16::from_le_bytes([b[16], b[17]]) != 3
        || u16::from_le_bytes([b[18], b[19]]) != 183
        || u32::from_le_bytes(b[20..24].try_into().unwrap()) != 1
        || u16::from_le_bytes([b[52], b[53]]) as usize != EHDR
    { return false; }

    let phoff = u64::from_le_bytes(b[32..40].try_into().unwrap());
    let shoff = u64::from_le_bytes(b[40..48].try_into().unwrap());
    let phentsize = u16::from_le_bytes([b[54], b[55]]) as usize;
    let phnum = u16::from_le_bytes([b[56], b[57]]) as usize;
    let shentsize = u16::from_le_bytes([b[58], b[59]]) as usize;
    let shnum = u16::from_le_bytes([b[60], b[61]]) as usize;
    let shstrndx = u16::from_le_bytes([b[62], b[63]]) as usize;

    let range_ok = |offset: u64, size: u64| -> bool {
        offset.checked_add(size).is_some_and(|end| end <= length as u64)
    };
    if phentsize != PHDR || shentsize != SHDR || shnum == 0 || shstrndx >= shnum
        || !range_ok(phoff, (PHDR as u64).saturating_mul(phnum as u64))
        || !range_ok(shoff, (SHDR as u64).saturating_mul(shnum as u64))
    { return false; }

    let sh_start = shoff as usize;
    let shdr = |index: usize| -> &[u8] {
        let start = sh_start + index * SHDR;
        &b[start..start + SHDR]
    };
    let shstr = shdr(shstrndx);
    let shstr_type = u32::from_le_bytes(shstr[4..8].try_into().unwrap());
    let shstr_off = u64::from_le_bytes(shstr[24..32].try_into().unwrap());
    let shstr_size = u64::from_le_bytes(shstr[32..40].try_into().unwrap());
    if shstr_type != 3 || !range_ok(shstr_off, shstr_size) { return false; }
    let names = &b[shstr_off as usize..(shstr_off + shstr_size) as usize];

    for index in 0..shnum {
        let s = shdr(index);
        let name_off = u32::from_le_bytes(s[0..4].try_into().unwrap()) as usize;
        let kind = u32::from_le_bytes(s[4..8].try_into().unwrap());
        let offset = u64::from_le_bytes(s[24..32].try_into().unwrap());
        let size = u64::from_le_bytes(s[32..40].try_into().unwrap());
        let link = u32::from_le_bytes(s[40..44].try_into().unwrap()) as usize;
        let entsize = u64::from_le_bytes(s[56..64].try_into().unwrap());
        if name_off >= names.len() || !names[name_off..].contains(&0) { return false; }
        // SHT_NOBITS has no corresponding file payload.
        if kind != 8 && !range_ok(offset, size) { return false; }
        if kind == 2 || kind == 11 {
            if entsize != SYM as u64 || size % SYM as u64 != 0 || link >= shnum { return false; }
            let linked = shdr(link);
            if u32::from_le_bytes(linked[4..8].try_into().unwrap()) != 3 { return false; }
            let str_off = u64::from_le_bytes(linked[24..32].try_into().unwrap());
            let str_size = u64::from_le_bytes(linked[32..40].try_into().unwrap());
            if !range_ok(str_off, str_size) { return false; }
            let strings = &b[str_off as usize..(str_off + str_size) as usize];
            let sym_count = (size / SYM as u64) as usize;
            let sym_start = offset as usize;
            for sym_index in 0..sym_count {
                let name = u32::from_le_bytes(
                    b[sym_start + sym_index * SYM..sym_start + sym_index * SYM + 4]
                        .try_into().unwrap()
                ) as usize;
                if name >= strings.len() || !strings[name..].contains(&0) { return false; }
            }
        }
        if kind == 5 {
            if size < 8 { return false; }
            let hash = &b[offset as usize..(offset + size) as usize];
            let buckets = u32::from_le_bytes(hash[0..4].try_into().unwrap()) as u64;
            let chains = u32::from_le_bytes(hash[4..8].try_into().unwrap()) as u64;
            if 8u64.checked_add(4 * (buckets + chains)).is_none_or(|required| required > size) {
                return false;
            }
        }
        if kind == 0x6ffffff6 {
            if size < 16 { return false; }
            let hash = &b[offset as usize..(offset + size) as usize];
            let buckets = u32::from_le_bytes(hash[0..4].try_into().unwrap()) as u64;
            let bloom = u32::from_le_bytes(hash[8..12].try_into().unwrap()) as u64;
            if bloom == 0 || buckets == 0
                || 16u64.checked_add(8 * bloom + 4 * buckets).is_none_or(|required| required > size)
            { return false; }
        }
    }
    true
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
    fn validates_elf64_aarch64_header_and_table_bounds() {
        let mut elf = [0u8; 184];
        elf[0..4].copy_from_slice(b"\x7fELF");
        elf[4] = 2;
        elf[5] = 1;
        elf[6] = 1;
        elf[16..18].copy_from_slice(&3u16.to_le_bytes());
        elf[18..20].copy_from_slice(&183u16.to_le_bytes());
        elf[20..24].copy_from_slice(&1u32.to_le_bytes());
        elf[32..40].copy_from_slice(&64u64.to_le_bytes());
        elf[40..48].copy_from_slice(&120u64.to_le_bytes());
        elf[52..54].copy_from_slice(&64u16.to_le_bytes());
        elf[54..56].copy_from_slice(&56u16.to_le_bytes());
        elf[56..58].copy_from_slice(&1u16.to_le_bytes());
        elf[58..60].copy_from_slice(&64u16.to_le_bytes());
        elf[60..62].copy_from_slice(&1u16.to_le_bytes());
        elf[62..64].copy_from_slice(&0u16.to_le_bytes());
        assert!(unsafe { omg_loader_core_validate_elf64_aarch64(elf.as_ptr(), elf.len()) });
        assert!(!unsafe { omg_loader_core_validate_elf64_aarch64(elf.as_ptr(), 63) });
        assert!(!unsafe { omg_loader_core_validate_elf64_aarch64(std::ptr::null(), elf.len()) });
        assert!(!unsafe { omg_loader_core_validate_elf64_aarch64(elf.as_ptr(), 64) });
        elf[18..20].copy_from_slice(&62u16.to_le_bytes());
        assert!(!unsafe { omg_loader_core_validate_elf64_aarch64(elf.as_ptr(), elf.len()) });
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
