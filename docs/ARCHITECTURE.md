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
- The low-level stream I/O and file-descriptor receive helpers in
  `loader/src/common/socket_utils.c` are implemented in C11. The existing
  C++ call sites use temporary inline compatibility wrappers in
  `socket_utils.hpp`; the socket implementation itself is no longer C++.
- The crate exports a versioned C ABI and builds as both an Rust library and
  an Android ARM64 static library.
- CMake builds the Rust static library with cargo-ndk and links it into the
  ptrace monitor executable.
- The monitor's Unix datagram control-packet framing and command validation
  now run through the Rust decoder. C++ retains socket I/O and dispatches the
  validated command to existing platform operations.
- Monitor lifecycle transition rules and zygote crash-loop accounting are
  owned by the Rust core and exposed through the same narrow C ABI. C++ retains
  monotonic clock reads, event-loop, ptrace and status-file side effects.
- Android linker, ptrace, JNI and hook adapters remain in C++ until their
  replacements are individually migrated and device-validated; the complete
  injector has not yet been ported.

## Independence and scope

OmgZygisk should own its runtime architecture rather than preserve an upstream
framework as a permanent outer shell. Upstream code is treated as a source of
specific compatible interfaces or independently licensed components, not as
an architectural template to reproduce wholesale. Keep only the Zygisk/ZN ABI
contracts and platform integration that are required for compatibility.

Keep existing process-isolation and visibility controls behind one documented
policy/configuration boundary. This is a maintainability goal: do not add
covert persistence, security-tool evasion, or behavior that bypasses Android
or root-manager security controls.

## Suggested migration order

1. Keep the C ABI narrow and verify every Android ARM64 link.
2. Continue moving platform-independent protocol parsing and lifecycle policy
   into the Rust core; avoid maintaining duplicate C++ policy.
3. Convert remaining generic native utilities to C11 where they do not depend
   on C++ ownership or platform-specific C++ ABI contracts.
4. Keep linker, JNI, ptrace and hook integration isolated behind narrow native
   adapters; do not rewrite those paths solely to change language.
5. Inventory upstream-derived wrappers and remove layers that only forward
   calls or duplicate state; retain required ABI shims and license notices.
6. Consolidate existing process-isolation configuration and policy without
   adding new stealth or security-evasion behavior.
