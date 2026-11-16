# Runtime footprint and performance audit

This document separates build-time size measurements from device-side runtime
measurements. A successful CI build is not evidence of lower CPU or RAM use.

## Scope

The supported optimization targets are ordinary resource consumption:

- daemon CPU time while idle and while handling a fixed workload;
- resident and proportional memory (RSS/PSS);
- thread count and thread lifetime;
- wakeups, timer frequency, polling and repeated directory scans;
- number and lifetime of helper processes;
- flashable ZIP size and uncompressed native payload size.

Do not treat process invisibility, altered mapping visibility, trace removal or
security-tool evasion as performance objectives.

## Current CI measurement

The Android release job runs `scripts/report_release_footprint.py` after
packaging. It records:

- final ZIP size on disk;
- total uncompressed and compressed entry sizes;
- aggregate native payload size;
- largest package entries.

The report is observational and does not modify the archive. Compare reports
from the same toolchain, commit base and build variant. CI does not measure
runtime CPU, memory, thread count or install-time decompression latency.

## Device-side baseline protocol

Use the same physical device, Android build, root manager, module set and
workload for baseline and candidate builds. Reboot between runs and collect
at least three samples per state.

1. Idle: wait until boot activity settles; collect a 10-minute sample.
2. Active: run a fixed sequence of module enumeration, status refresh and
   representative app launches; collect a 5-minute sample.
3. Record daemon PID, process CPU time, RSS/PSS, thread count, context
   switches and wakeups at a fixed interval.
4. Record helper-process start/exit events and their cumulative CPU time.
5. Report median and range, not only the best sample.
6. Confirm functional behavior and logs before interpreting a lower resource
   figure as an improvement.

Useful Android/Linux sources include `dumpsys meminfo`, `/proc/<pid>/status`,
`/proc/<pid>/stat`, `/proc/<pid>/task/` and `simpleperf`. Collection should
be performed with the same permissions and sampling interval for each build.

## Optimization order

1. Remove redundant periodic work only after identifying its correctness
   purpose and proving an event-driven alternative preserves required updates.
2. Bound caches and queues; avoid unbounded growth and duplicate ownership.
3. Reuse buffers on measured hot paths; avoid allocating temporary strings or
   vectors for fixed-size protocol fields.
4. Make worker lifetime explicit; avoid idle threads where synchronous or
   event-driven work is sufficient.
5. Keep the Rust/C boundary narrow and batch calls where crossing the boundary
   is measurable overhead.
6. Measure each change independently. Revert changes that trade correctness,
   compatibility or reliability for unproven savings.

## Language policy

Use C for small platform-neutral primitives when a stable C ABI is useful.
Use Rust for parsing, state machines, bounded protocol handling and ownership
that benefits from its type system. Retain C++ only where an existing platform
or third-party ABI requires it. Do not mechanically rename C++ files: templates,
RAII, exceptions, callbacks and object lifetimes require an explicit design and
tests before conversion.

The Android injection, ptrace, linker and hook execution paths are not covered
by this optimization plan. Changes to those paths require a separate safety
and compatibility review rather than a language-only rewrite.
