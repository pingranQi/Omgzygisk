//! Stable C ABI for the Android-side monitor adapter.
use crate::monitor::{Monitor, TracingState};

/// Opaque monitor state owned by Rust.
pub struct OmgMonitorHandle {
    inner: Monitor<'static>,
}

#[unsafe(no_mangle)]
pub extern "C" fn omg_monitor_create() -> *mut OmgMonitorHandle {
    Box::into_raw(Box::new(OmgMonitorHandle { inner: Monitor::default() }))
}

/// # Safety
/// handle must be null or a live pointer returned by omg_monitor_create,
/// and must not be used concurrently while it is being destroyed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_destroy(handle: *mut OmgMonitorHandle) {
    if !handle.is_null() {
        // SAFETY: guaranteed by this function's contract.
        unsafe { drop(Box::from_raw(handle)); }
    }
}

/// # Safety
/// handle must be null or point to a live monitor returned by
/// omg_monitor_create. The caller must not mutate or destroy it concurrently.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_state(handle: *const OmgMonitorHandle) -> u32 {
    // SAFETY: guaranteed by this function's contract.
    let Some(handle) = (unsafe { handle.as_ref() }) else { return 0; };
    match handle.inner.state {
        TracingState::Tracing => 1,
        TracingState::Stopping => 2,
        TracingState::Stopped => 3,
        TracingState::Exiting => 4,
    }
}

/// # Safety
/// handle must be null or point to a live monitor returned by
/// omg_monitor_create. Calls using the same handle must be serialized.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_request_start(handle: *mut OmgMonitorHandle) {
    // SAFETY: guaranteed by this function's contract.
    if let Some(handle) = unsafe { handle.as_mut() } { handle.inner.request_start(); }
}

/// # Safety
/// handle must be null or point to a live monitor returned by
/// omg_monitor_create. Calls using the same handle must be serialized.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_request_stop(handle: *mut OmgMonitorHandle) {
    // SAFETY: guaranteed by this function's contract.
    if let Some(handle) = unsafe { handle.as_mut() } { handle.inner.request_stop("requested"); }
}

/// # Safety
/// handle must be null or point to a live monitor returned by
/// omg_monitor_create. Calls using the same handle must be serialized.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_request_exit(handle: *mut OmgMonitorHandle) {
    // SAFETY: guaranteed by this function's contract.
    if let Some(handle) = unsafe { handle.as_mut() } { handle.inner.request_exit(); }
}

/// # Safety
/// handle must be null or point to a live monitor returned by
/// omg_monitor_create. Calls using the same handle must be serialized.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_notify_detached(handle: *mut OmgMonitorHandle) {
    // SAFETY: guaranteed by this function's contract.
    if let Some(handle) = unsafe { handle.as_mut() } { handle.inner.notify_detached(); }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn c_abi_handle_round_trips_lifecycle_states() {
        let handle = omg_monitor_create();
        assert!(!handle.is_null());
        // SAFETY: handle was created above and is exclusively used here.
        unsafe {
            assert_eq!(omg_monitor_state(handle), 1);
            omg_monitor_request_stop(handle);
            assert_eq!(omg_monitor_state(handle), 2);
            omg_monitor_notify_detached(handle);
            assert_eq!(omg_monitor_state(handle), 3);
            omg_monitor_request_start(handle);
            assert_eq!(omg_monitor_state(handle), 1);
            omg_monitor_request_exit(handle);
            assert_eq!(omg_monitor_state(handle), 4);
            omg_monitor_destroy(handle);
        }
    }

    #[test]
    fn c_abi_handles_null_pointers_without_dereferencing() {
        // SAFETY: null is explicitly accepted by every pointer-taking function.
        unsafe {
            assert_eq!(omg_monitor_state(std::ptr::null()), 0);
            omg_monitor_request_start(std::ptr::null_mut());
            omg_monitor_request_stop(std::ptr::null_mut());
            omg_monitor_request_exit(std::ptr::null_mut());
            omg_monitor_notify_detached(std::ptr::null_mut());
            omg_monitor_destroy(std::ptr::null_mut());
        }
    }
}