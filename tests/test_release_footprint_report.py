#!/usr/bin/env python3
"""Unit tests for the observational release ZIP footprint report."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "report_release_footprint.py"
SPEC = importlib.util.spec_from_file_location("report_release_footprint", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
REPORT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REPORT)


class ReleaseFootprintReportTests(unittest.TestCase):
    def test_human_formats_binary_units(self) -> None:
        self.assertEqual(REPORT.human(0), "0.00 B")
        self.assertEqual(REPORT.human(1024), "1.00 KiB")
        self.assertEqual(REPORT.human(1024 * 1024), "1.00 MiB")

    def test_main_reports_archive_and_native_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive_path = Path(directory) / "module.zip"
            with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
                archive.writestr("module.prop", "id=example\n")
                archive.writestr("lib/arm64-v8a/libexample.so", "native-payload")
                archive.writestr("assets/readme.txt", "documentation")

            original_argv = sys.argv
            output = io.StringIO()
            try:
                sys.argv = [str(SCRIPT), str(archive_path), "--top", "2"]
                with contextlib.redirect_stdout(output):
                    result = REPORT.main()
            finally:
                sys.argv = original_argv

            report = output.getvalue()
            self.assertEqual(result, 0)
            self.assertIn("module.zip", report)
            self.assertIn("Native payloads (uncompressed)", report)
            self.assertIn("lib/arm64-v8a/libexample.so", report)
            self.assertIn("Largest 2 entries", report)
            self.assertIn("does not alter ZIP contents", report)


if __name__ == "__main__":
    unittest.main()
