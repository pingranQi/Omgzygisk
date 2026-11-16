# Rust loader core

This crate is the platform-independent state and protocol core used by the ARM64 native monitor adapter.

It owns bounded IPC/control packet parsing, daemon action discriminants, descriptor ownership primitives, and the monitor lifecycle state machine. The monitor state is exposed to the C adapter through the small ABI in `src/ffi.rs`; Android syscalls, ptrace, linker operations, JNI, and hook execution remain in the native platform layer.

The Android ARM64 build compiles this crate as a static library and links it into `libzygisk_ptrace.so`. The crate is also host-testable and must remain free of Android-only dependencies.

Run the tests with:

```sh
cargo test
```

The Android build requires the Rust target `aarch64-linux-android` to be installed.