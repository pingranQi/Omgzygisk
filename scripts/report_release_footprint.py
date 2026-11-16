#!/usr/bin/env python3
"""Report release ZIP footprint without modifying the package."""
from __future__ import annotations

import argparse
import zipfile
from pathlib import Path


def human(size: int) -> str:
    value = float(size)
    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024 or unit == "GiB":
            return f"{value:.2f} {unit}"
        value /= 1024
    return f"{value:.2f} GiB"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    if not args.archive.is_file():
        parser.error(f"archive does not exist: {args.archive}")

    with zipfile.ZipFile(args.archive) as archive:
        files = [entry for entry in archive.infolist() if not entry.is_dir()]
        raw_total = sum(entry.file_size for entry in files)
        compressed_total = sum(entry.compress_size for entry in files)
        native = [
            entry for entry in files
            if entry.filename.endswith((".so", ".so.xz", "/zygiskd", "/libzygisk_ptrace.so"))
        ]

        print(f"## Release package footprint: {args.archive.name}")
        print()
        print("| Metric | Size |")
        print("|---|---:|")
        print(f"| ZIP on disk | {human(args.archive.stat().st_size)} |")
        print(f"| Entries uncompressed | {human(raw_total)} |")
        print(f"| Entries compressed | {human(compressed_total)} |")
        print(f"| Native payloads (uncompressed) | {human(sum(x.file_size for x in native))} |")
        print(f"| File count | {len(files)} |")
        print()
        print(f"### Largest {args.top} entries (uncompressed)")
        print()
        print("| Entry | Uncompressed | Compressed |")
        print("|---|---:|---:|")
        for entry in sorted(files, key=lambda item: item.file_size, reverse=True)[:args.top]:
            print(
                f"| `{entry.filename.replace('|', '&#124;')}` "
                f"| {human(entry.file_size)} | {human(entry.compress_size)} |"
            )
        print()
        print("This report is observational; it does not alter ZIP contents or installation behavior.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
