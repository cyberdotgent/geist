#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
"""Reject non-universal, newer-minimum or externally linked release bundles."""
import pathlib
import re
import subprocess
import sys


def verify(app):
    binaries = []
    for path in app.rglob("*"):
        if path.is_file() and not path.is_symlink():
            kind = subprocess.check_output(["file", "-b", str(path)], text=True)
            if "Mach-O" in kind:
                binaries.append(path)
    if not binaries:
        raise ValueError("No Mach-O executable in bundle")
    for path in binaries:
        subprocess.run(["lipo", str(path), "-verify_arch", "arm64", "x86_64"], check=True)
        for arch in ("arm64", "x86_64"):
            commands = subprocess.check_output(["otool", "-arch", arch, "-l", str(path)], text=True)
            minimums = re.findall(r"\bminos\s+(\d+(?:\.\d+)*)", commands)
            if not minimums or any(tuple(map(int, v.split("."))) > (13, 0, 0)
                                   for v in minimums):
                raise ValueError(f"Invalid Ventura deployment target: {path} {arch}: {minimums}")
            imports = subprocess.check_output(["otool", "-arch", arch, "-L", str(path)], text=True)
            for line in imports.splitlines():
                if "(compatibility version" in line:
                    name = line.strip().split(" (", 1)[0]
                    if not name.startswith(("/usr/lib/", "/System/Library/")):
                        raise ValueError(f"Non-system dependency: {path}: {name}")


if __name__ == "__main__":
    verify(pathlib.Path(sys.argv[1]))
