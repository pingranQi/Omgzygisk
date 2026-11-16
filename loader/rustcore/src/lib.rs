//! Platform-independent core for OmgZygisk's native loader.
//!
//! Android-specific syscalls and ABI adapters remain in the C/C++ boundary;
//! protocol validation and lifecycle policy live in this crate.

pub mod control;
pub mod fd;
pub mod ffi;
pub mod ipc;
pub mod monitor;
