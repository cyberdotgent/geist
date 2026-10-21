#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
"""Portable negative checks for the release verifier; no Apple tools needed."""
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "verify_release", pathlib.Path(__file__).resolve().parents[1] / "macos/verify_release.py")
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class VerifyRelease(unittest.TestCase):
    def check(self, minimum="13.0", dependency="/usr/lib/libSystem.B.dylib", missing_slice=False):
        def output(args, **kwargs):
            if args[0] == "file":
                return "Mach-O universal binary"
            if "-l" in args:
                return f"cmd LC_BUILD_VERSION\n minos {minimum}\n sdk 15.0\n"
            return f"app:\n\t{dependency} (compatibility version 1.0.0, current version 1.0.0)\n"

        with tempfile.TemporaryDirectory() as directory:
            app = pathlib.Path(directory)
            (app / "executable").touch()
            with patch.object(subprocess, "check_output", side_effect=output), \
                 patch.object(subprocess, "run") as run:
                if missing_slice:
                    run.side_effect = subprocess.CalledProcessError(1, "lipo")
                verifier.verify(app)
                self.assertEqual(run.call_args.args[0][1:4],
                                 ["-verify_arch", "arm64", "x86_64"])

    def test_ventura(self):
        self.check()

    def test_newer_minimum(self):
        with self.assertRaises(ValueError):
            self.check(minimum="14.0")

    def test_unproven_minimum(self):
        with self.assertRaises(ValueError):
            self.check(minimum="unknown")

    def test_homebrew_dependency(self):
        with self.assertRaises(ValueError):
            self.check(dependency="/opt/homebrew/lib/libpng16.dylib")

    def test_missing_slice(self):
        with self.assertRaises(subprocess.CalledProcessError):
            self.check(missing_slice=True)

    def test_empty_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                verifier.verify(pathlib.Path(directory))


if __name__ == "__main__":
    unittest.main()
