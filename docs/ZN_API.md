# Native ZN API compatibility status

**Status: not implemented.** This is a compatibility assessment, not a claim
that this build can load Native ZN modules.

YukiZygisk's Native ZN model is a separate native-process runtime, not another
callback table inside the existing Zygisk module loader. Its integration
includes:

- a per-module `zn_modules.txt` manifest with executable-name or executable-path
  targets and a library path;
- the exported `zn_module` ABI and `onModuleLoaded` callback;
- daemon-side discovery, target matching, file-descriptor delivery and optional
  companion transport;
- an exec-time bootstrap that runs before the target executable's entry point,
  followed by a native Core and its own loading, hook and retirement lifecycle.

OnyxZygisk currently attaches its tracer to the init/stub process tree to detect
and inject Android Zygote. Its existing `libzygisk.so` runtime handles Zygisk
modules in Zygote-derived processes, while FN nodes are driven by the Onyx
daemon. It does not currently provide a general exec-time bootstrap for
arbitrary native executables. Adding only a public ZN header, a manifest parser,
or a WebUI toggle would advertise support that cannot work.

A real port needs, at minimum, a process-exec bootstrap path, a daemon protocol
for native module catalogs and per-process delivery, a versioned native ABI, a
native-process Core with symbol resolution and hook services, and explicit
module/companion lifetime handling. These pieces must be implemented and
tested together. Until then, the WebUI reports Native ZN as unavailable.

The VMA placement change is independent: it prefers a guarded high-address gap
for the custom `libzygisk.so` mapping and LSPlt backup mappings. Each preferred
address is passed as a normal `mmap` hint; the kernel validates the reservation.
A rejected hint is released and the code falls back without using a fixed
mapping to overwrite an unrelated VMA. This policy optimizes address-space
layout; device benchmarks are still required to establish a performance gain.
