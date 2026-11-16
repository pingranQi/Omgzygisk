#!/usr/bin/env python3
"""Report release ZIP footprint without modifying the package."""
from __future__ import annotations

import argparse
import hashlib
import zipfile
from collections import defaultdict
from pathlib import Path
from typing import Iterable
from zipfile import ZipInfo

ABIS = ("arm64-v8a", "armeabi-v7a", "x86_64", "x86")


def human(size: int) -> str:
    value = float(size)
    for unit in ("B", "KiB", "MiB", "GiB"):
        if value < 1024 or unit == "GiB":
            return f"{value:.2f} {unit}"
        value /= 1024
    return f"{value:.2f} GiB"


def abi_for(filename: str) -> str:
    """Return the ABI path component, or the shared/unclassified bucket."""
    parts = Path(filename).parts
    for abi in ABIS:
        if abi in parts:
            return abi
    return "shared / unclassified"


def native_payload(entry: ZipInfo) -> bool:
    return entry.filename.endswith((".so", ".so.xz", "/zygiskd", "/libzygisk_ptrace.so"))


def duplicate_content_groups(archive: zipfile.ZipFile, entries: Iterable[ZipInfo]):
    """Find byte-identical files using bounded-memory streaming hashes."""
    groups: dict[str, list[ZipInfo]] = defaultdict(list)
    for entry in entries:
        digest = hashlib.sha256()
        with archive.open(entry) as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        groups[digest.hexdigest()].append(entry)
    return [group for group in groups.values() if len(group) > 1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    if args.top < 1:
        parser.error("--top must be at least 1")
    if not args.archive.is_file():
        parser.error(f"archive does not exist: {args.archive}")

    with zipfile.ZipFile(args.archive) as archive:
        files = [entry for entry in archive.infolist() if not entry.is_dir()]
        raw_total = sum(entry.file_size for entry in files)
        compressed_total = sum(entry.compress_size for entry in files)
        native = [entry for entry in files if native_payload(entry)]

        by_abi: dict[str, list[ZipInfo]] = defaultdict(list)
        for entry in files:
            by_abi[abi_for(entry.filename)].append(entry)

        duplicate_groups = duplicate_content_groups(archive, files)
        duplicate_bytes = sum(
            sum(entry.file_size for entry in group) - max(entry.file_size for entry in group)
            for group in duplicate_groups
        )

        print(f"## Release package footprint: {args.archive.name}")
        print()
        print("| Metric | Size |")
        print("|---|---:|")
        print(f"| ZIP on disk | {human(args.archive.stat().st_size)} |")
        print(f"| Entries uncompressed | {human(raw_total)} |")
        print(f"| Entries compressed | {human(compressed_total)} |")
        print(f"| Native payloads (uncompressed) | {human(sum(x.file_size for x in native))} |")
        print(f"| Identical-content duplicate bytes (uncompressed) | {human(duplicate_bytes)} |")
        print(f"| File count | {len(files)} |")
        print()
        print("### Size by ABI path")
        print()
        print("| ABI bucket | Files | Uncompressed | Compressed | Native |")
        print("|---|---:|---:|---:|---:|")
        for abi, entries in sorted(by_abi.items()):
            print(
                f"| {abi} | {len(entries)} | {human(sum(x.file_size for x in entries))} "
                f"| {human(sum(x.compress_size for x in entries))} "
                f"| {human(sum(x.file_size for x in entries if native_payload(x)))} |"
            )
        print()
        print(f"### Largest {args.top} entries (uncompressed)")
        print()
        print("| Entry | ABI bucket | Uncompressed | Compressed |")
        print("|---|---|---:|---:|")
        for entry in sorted(files, key=lambda item: item.file_size, reverse=True)[:args.top]:
            safe_name = entry.filename.replace("|", "&#124;")
            print(
                f"| `{safe_name}` | {abi_for(entry.filename)} "
                f"| {human(entry.file_size)} | {human(entry.compress_size)} |"
            )
        print()
        print("### Identical-content duplicates")
        print()
        if not duplicate_groups:
            print("No byte-identical duplicate files found.")
        else:
            for group in sorted(
                duplicate_groups,
                key=lambda items: sum(item.file_size for item in items) - max(item.file_size for item in items),
                reverse=True,
            )[:args.top]:
                redundant = sum(item.file_size for item in group) - max(item.file_size for item in group)
                print(f"- {human(redundant)} redundant: " + ", ".join(f"`{x.filename}`" for x in group))
        print()
        print("This report is observational; it does not alter ZIP contents or installation behavior.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
