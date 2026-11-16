# OmgZygisk architecture

OmgZygisk is being organized around **C for the Android-native boundary and
Rust for policy, protocol, and lifecycle logic**. The migration is incremental:
existing behavior and module ABI compatibility take priority over a wholesale
rewrite.

## Language ownership

| Layer | Primary language | Responsibilities |
| --- | --- | --- |
| Android loader boundary | C | Small ABI-facing shims, raw syscall wrappers, memory and descriptor primitives where a C ABI is required |
| Native injection and platform adapters | C, with existing C++ compatibility code during migration | Zygote entry points, ptrace, linker integration, JNI and hook-library adapters |
| Loader core | Rust | Bounded protocol decoding, monitor state transitions, descriptor ownership helpers, validation and platform-independent policy |
| Privileged daemon | Rust | IPC, module discovery, manifest validation, companion lifecycle, mount policy and root-manager integration |
| Build and packaging | Gradle + CMake + Cargo | NDK builds, Rust cross-compilation, ABI packaging and release assembly |
| Web management UI | TypeScript | UI only; it is not part of the native runtime core |

## FFI contract

The Rust loader core exposes a deliberately small C ABI in
loader/rustcore/include/omg_loader_core.h. Only fixed-width integers, booleans,
pointers and lengths cross this boundary. Rust-owned allocations, references,
trait objects and standard-library types must not cross it.

The exported ABI is versioned. Callers should check
omg_loader_core_abi_version() before using optional functions. Input buffers
remain caller-owned, are borrowed only for the duration of a call, and must be
length-bounded before parsing.

## Migration rules

1. **Do not rewrite stable platform code just to change its language.** Keep
   ptrace, linker and hook adapters thin and isolated while their behavior is
   validated on Android.
2. **Move policy before mechanics.** Protocol decoding, state machines,
   manifest parsing, path validation and error classification belong in Rust;
   syscalls and ABI callbacks remain in the native adapter.
3. **One wire format, one owner.** Keep daemon action numbers and packet
   layouts defined once, with tests on both sides of the FFI boundary.
4. **No Rust ABI leakage.** All cross-language calls use the C header. Do not
   expose Rust enums, String, Vec, references or panic unwinding to C.
5. **Preserve compatibility.** Existing Zygisk/ZN entry symbols, API version
   tables, module directory, socket protocol values and release package layout
   are compatibility constraints.
6. **Gate each migration slice.** Rust unit tests, native compile checks,
   Android arm64 package builds and device smoke tests are required before
   retiring the corresponding legacy implementation.

## Current status

- The daemon is already implemented in Rust.
- The loader has a platform-independent Rust crate for control packets, IPC
  primitives, descriptor ownership and monitor state transitions.
- The crate exports a versioned C ABI and builds as both an Rust library and
  an Android ARM64 static library.
- CMake builds the Rust static library with cargo-ndk and links it into the
  ptrace monitor executable.
- The monitor's Unix datagram control-packet framing and command validation
  now run through the Rust decoder. C++ retains socket I/O and dispatches the
  validated command to existing platform operations.
- Android linker, ptrace, JNI and hook adapters remain in C++ until their
  replacements are individually migrated and device-validated; the complete
  injector has not yet been ported.

## Suggested migration order

1. Link the Rust static library into the Android native build and add a
   link-time ABI smoke test.
2. Move the loader's protocol parsing and monitor policy behind the new FFI.
3. Move platform-independent ELF metadata parsing into Rust while retaining
   linker-specific accessors in C.
4. Convert isolated C++ utility modules to C where they use no C++ runtime.
5. Reduce the remaining C++ surface to unavoidable third-party or Android ABI
   adapters, with explicit ownership and thread-safety contracts.
