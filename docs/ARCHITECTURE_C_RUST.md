# C + Rust Architecture

## Goal

Make C and Rust the primary implementation languages for OmgZygisk. Keep C++ out of project-owned core logic except where a third-party dependency or an unavoidable ABI boundary requires it.

This is an incremental migration. It must preserve the existing Zygisk/ZN ABI, daemon wire protocol, module package layout, ptrace lifecycle, and Android compatibility.

## Target ownership

| Layer | Primary language | Responsibility |
| --- | --- | --- |
| Android process adapter | C | ptrace, process bootstrap, syscalls, linker/hook integration, JNI and Android-specific glue |
| Portable loader core | Rust | bounded IPC decoding, protocol validation, descriptor ownership, monitor state, ELF metadata validation and policy |
| Daemon | Rust | module discovery, manifest validation, IPC, companion lifecycle, filesystem policy and WebUI serving |
| WebUI | TypeScript | management interface only; no native runtime logic |
| Third-party compatibility | Existing upstream language | Keep vendored dependencies isolated behind narrow C ABI wrappers |

## Migration rules

1. Do not translate C++ mechanically. Separate platform effects from policy/state logic first.
2. Move portable parsing, validation, state machines, and ownership into Rust modules with host-side unit tests.
3. Keep Android syscalls and ptrace operations in a small C adapter layer. Every FFI boundary must use fixed-width types, explicit buffer lengths, and documented ownership.
4. Convert project-owned C++ implementation files to C where they are procedural and do not depend on C++ object models. Replace C++ containers/strings in those paths with bounded C interfaces.
5. Keep unavoidable upstream C++ (for example, hook libraries) as isolated dependencies; do not let their types leak into project-owned APIs.
6. Preserve numeric protocol discriminants and exported ABI symbols. Any protocol change must be append-only and covered by compatibility tests.
7. No behavior change is bundled with a language migration unless it has an independent test and review.
8. Every stage must pass Rust unit tests, native/JNI checks, WebUI checks, and the Android ARM64 release build before merge.

## Current baseline

- `zygiskd/` is already a Rust daemon.
- `loader/rustcore/` is linked into the ARM64 Android ptrace binary as a Rust static library; the monitor lifecycle is routed through its C ABI.
- The procedural ptrace cleanup helper and Unix socket transport primitives are implemented in C. The C++ socket header is now a thin convenience wrapper over the C ABI.
- `loader/src/` still contains substantial project-owned C++ code, especially injector, ELF/linker, and ptracer orchestration units; these remain the main migration target.
- CMake enables both C and C++; the Android Gradle toolchain supplies the C18 dialect flag, while C++ remains on C++23.

## Staged plan

### Stage 1 — Make Rust core a production component

**Status: initial integration complete; broader core migration remains in progress.**

- Turn `loader/rustcore` into a Rust library with a small, stable C ABI.
- Link it into the ARM64 Android native build.
- Route monitor lifecycle state transitions through Rust.
- Keep syscalls, ptrace, and Android linker operations in the native adapter.
- Continue moving protocol validation and descriptor ownership from C++ wrappers into Rust.

### Stage 2 — Move procedural native code to C

**Status: started.**

- Convert independent procedural `.cpp` units to `.c`; the ptrace cleanup and socket transport primitives are the first converted units.
- Replace C++-only headers with C-compatible headers; retain only small inline C++ convenience wrappers where current callers still use C++ strings.
- Keep classes and upstream C++ dependencies behind narrow adapter boundaries until each subsystem has a tested C/Rust replacement.
- Next candidates are filesystem/proc parsing helpers and ptracer utilities, after their ownership and call contracts are separated from C++ containers.

### Stage 3 — Move policy and data ownership to Rust

- Migrate ELF metadata parsing, process/manifest matching, loader policy, and lifecycle state to Rust.
- Expose only small C ABI calls to the Android adapter.
- Remove duplicated C++ state and protocol definitions after cross-language parity tests pass.

### Stage 4 — Reduce and isolate C++ compatibility surface

- Inventory remaining C++ translation units and classify each as upstream/vendor, ABI shim, or project-owned.
- Remove project-owned C++ core code where a tested C/Rust implementation exists.
- Document every retained C++ unit and its reason for retention.

## Non-goals

- No change to the public Zygisk or Native ZN ABI.
- No change to module IDs, install paths, daemon protocol numbers, or release package structure.
- No blanket rewrite of vendored third-party code.
- No claim of HyperOS device compatibility without physical-device validation.