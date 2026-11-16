//! Stable C ABI for the Android-side monitor adapter.
use crate::monitor::{Monitor, TracingState};
use std::ptr;

pub struct OmgMonitorHandle { inner: Monitor<'static> }

#[unsafe(no_mangle)]
pub extern "C" fn omg_monitor_create() -> *mut OmgMonitorHandle {
    Box::into_raw(Box::new(OmgMonitorHandle { inner: Monitor::default() }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_destroy(handle: *mut OmgMonitorHandle) {
    if !handle.is_null() { unsafe { drop(Box::from_raw(handle)); } }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_state(handle: *const OmgMonitorHandle) -> u32 {
    let Some(handle) = (unsafe { handle.as_ref() }) else { return 0; };
    match handle.inner.state { TracingState::Tracing => 1, TracingState::Stopping => 2, TracingState::Stopped => 3, TracingState::Exiting => 4 }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_set_state(handle: *mut OmgMonitorHandle, state: u32) -> bool {
    let Some(handle) = (unsafe { handle.as_mut() }) else { return false; };
    handle.inner.state = match state { 1 => TracingState::Tracing, 2 => TracingState::Stopping, 3 => TracingState::Stopped, 4 => TracingState::Exiting, _ => return false };
    true
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_request_start(handle: *mut OmgMonitorHandle) {
    if let Some(handle) = unsafe { handle.as_mut() } { handle.inner.request_start(); }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_request_stop(handle: *mut OmgMonitorHandle) {
    if let Some(handle) = unsafe { handle.as_mut() } { handle.inner.request_stop("requested"); }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_request_exit(handle: *mut OmgMonitorHandle) {
    if let Some(handle) = unsafe { handle.as_mut() } { handle.inner.request_exit(); }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn omg_monitor_notify_detached(handle: *mut OmgMonitorHandle) {
    if let Some(handle) = unsafe { handle.as_mut() } { handle.inner.notify_detached(); }
}
