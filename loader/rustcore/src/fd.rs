//! Minimal ownership wrapper for a raw file descriptor.

use std::fs::File;
use std::os::fd::{FromRawFd, RawFd};

/// Owns one descriptor transferred by the caller.
///
/// init takes ownership of an already-open descriptor. take transfers
/// ownership back to the caller and prevents this type from closing it.
#[derive(Debug, Default)]
pub struct OwnedFd {
    value: Option<RawFd>,
}

impl OwnedFd {
    pub fn init(value: RawFd) -> Self {
        Self { value: Some(value) }
    }

    pub fn invalid() -> Self {
        Self { value: None }
    }

    pub fn is_valid(&self) -> bool {
        self.value.is_some()
    }

    pub fn get(&self) -> Option<RawFd> {
        self.value
    }

    pub fn take(&mut self) -> Option<RawFd> {
        self.value.take()
    }

    pub fn close(&mut self) {
        if let Some(value) = self.take() {
            if value >= 0 {
                // SAFETY: init transfers ownership of this open descriptor,
                // and take ensures it can only be converted into File once.
                unsafe {
                    drop(File::from_raw_fd(value));
                }
            }
        }
    }
}

impl Drop for OwnedFd {
    fn drop(&mut self) {
        self.close();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn invalid_descriptor_is_inert() {
        let mut fd = OwnedFd::invalid();
        assert!(!fd.is_valid());
        assert_eq!(fd.take(), None);
    }

    #[test]
    fn take_transfers_ownership() {
        let mut fd = OwnedFd::init(42);
        assert!(fd.is_valid());
        assert_eq!(fd.get(), Some(42));
        assert_eq!(fd.take(), Some(42));
        assert!(!fd.is_valid());
    }
}
