#!/usr/bin/env python3
"""Compare XZ with ZIP's raw-DEFLATE method for module payload files.

This is a measurement-only experiment: it does not modify the release package.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import zlib
from pathlib import Path

SKIP_SUFFIXES = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".ico", ".zip", ".gz", ".xz", ".zst"}
MIN_SIZE = 512


def deflate_size(data: bytes) -> int:
    compressor = zlib.compressobj(level=9, method=zlib.DEFLATED, wbits=-15)
    return len(compressor.compress(data) + compressor.flush())


def xz_size(data: bytes) -> int:
    result = subprocess.run(
        ["xz", "--compress", "--stdout", "--threads=1", "--lzma2=preset=6"],
        input=data,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    return len(result.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("module_dir", type=Path)
    args = parser.parse_args()

    root = args.module_dir.resolve()
    if not root.is_dir():
        parser.error(f"module directory does not exist: {root}")

    candidates = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.suffix.lower() in SKIP_SUFFIXES:
            continue
        if "ptrace" in path.name.lower():
            continue
        data = path.read_bytes()
        if len(data) < MIN_SIZE:
            continue
        candidates.append((path.relative_to(root).as_posix(), data))

    print("| File | Raw | ZIP/DEFLATE | XZ -6 | XZ vs ZIP |")
    print("|---|---:|---:|---:|---:|")
    raw_total = deflate_total = xz_total = 0
    for name, data in candidates:
        deflated = deflate_size(data)
        xz = xz_size(data)
        raw_total += len(data)
        deflate_total += deflated
        xz_total += xz
        delta = (xz - deflated) * 100 / deflated if deflated else 0
        print(f"| {name} | {len(data):,} | {deflated:,} | {xz:,} | {delta:+.1f}% |")

    if not candidates:
        print("| (no eligible files >= 512 bytes) | 0 | 0 | 0 | n/a |")
        return 0

    delta = (xz_total - deflate_total) * 100 / deflate_total if deflate_total else 0
    print(f"| **Total** | **{raw_total:,}** | **{deflate_total:,}** | **{xz_total:,}** | **{delta:+.1f}%** |")
    print()
    print("XZ is measured per file; this report does not change the ZIP or installer.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"XZ benchmark failed: {error}", file=sys.stderr)
        raise SystemExit(1)
