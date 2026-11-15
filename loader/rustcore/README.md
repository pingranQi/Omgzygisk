# Rust loader core

Platform-independent primitives extracted from the former Zig core prototype.
The crate contains bounded IPC decoding/encoding, monitor-control packet
validation, descriptor ownership, and a small monitor lifecycle state machine.

This crate deliberately performs no socket I/O, ptrace calls, Android linker
operations, JNI work, or mount-namespace changes. Those platform operations
remain outside this host-testable layer. It is not linked into the Android
shared libraries yet.

Run the tests with cargo test.

CMake can expose the same host-side check with
-DONYX_BUILD_RUSTCORE_TESTS=ON.
