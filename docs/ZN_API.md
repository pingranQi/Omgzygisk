# Native ZN API and HyperOS runtime

**Implementation status: added on `feature/zn-api-hyperos-runtime`; pending CI and on-device validation.**

This branch adds Native Zygisk Next (ZN) compatibility alongside the existing
Zygisk API. The implementation is based on the public ABI and process model
used by NyaZygisk and YukiZygisk; it does not import their private runtime or
kernel-side linker code.

## Native ZN loading path

- `zygiskd` scans enabled modules under `/data/adb/modules` for
  `zn_modules.txt`.
- Manifest targets support `name=<executable>` and
  `path=<absolute-executable-path>`; `companion` is an optional flag and
  the final token names the module library.
- Manifest paths are canonicalized and constrained to their owning module
  directory. Disabled/removed modules, non-ELF files, and wrong-ABI libraries
  are skipped.
- The init monitor checks manifest matches at process `exec`. Only matched
  processes are handed to the separate `trace-zn` bootstrap.
- The Native ZN loader resolves `zn_module`, calls `onModuleLoaded`, and
  exposes the API table versioned through v4. The existing Zygisk
  `zygisk_module_entry` / `entry` path remains separate.
- The daemon transfers sealed library memfds over the Unix socket using
  `SCM_RIGHTS`. Native ZN companion requests are checked against the
  requesting process and its manifest entry.

Daemon protocol values 0–13 are preserved. Native ZN commands are appended at
14–17 so older Zygisk clients retain their existing wire values.

## ZN API v4

The API table includes PLT and inline hooks, symbol resolver methods,
companion connection, and runtime discovery. PLT hooking uses the existing
LSPlt integration; inline hooks use Dobby. Symbol resolution extends the
project's existing ELF parser rather than adding a second parser.

Native ZN modules are kept resident for the lifetime of the target process.
The implementation does not attempt to unload module code while hooks or
callbacks may still refer to it.

## HyperOS runtime

The runtime is exposed only when the executable resolves to
`/system_ext/bin/hyos_spawner` (including a `/hyos_spawner` suffix). The
spawner PID is captured with the raw `getpid` syscall so fork/clone children
can be distinguished even when libc's cached PID is stale.

The runtime hooks `selinux_android_setcontext` and
`pthread_setname_np`, using PLT hooks with an inline-hook fallback. For a
non-system-server child, `onAppSpecialized` runs after the SELinux app
context call succeeds. The callback receives process name, package name, and
SELinux `se_info` strings valid for the callback duration.

## Validation limits

Host tests cover manifest parsing and existing Rust loader primitives. GitHub
Actions checks the Android package build and WebUI/JNI suites. Neither CI nor
source inspection can prove that HyperOS private `hyos_spawner` symbols are
available, have the expected ABI, or are hooked at the correct lifecycle point
on every HyperOS release. Device-side testing on supported HyperOS versions is
required before treating the runtime as production-verified.

The VMA placement change is independent: it prefers a guarded high-address gap
for the custom `libzygisk.so` mapping and LSPlt backup mappings. Each
preferred address is passed as a normal `mmap` hint; the kernel validates the
reservation. A rejected hint is released and the code falls back without
using a fixed mapping to overwrite an unrelated VMA.
