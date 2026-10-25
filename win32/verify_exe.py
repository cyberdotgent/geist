#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
"""Checks the Win32 reader executable is standalone and correctly manifested.

Reads the PE file directly, so the same check runs on the MinGW-w64 cross
build (Linux) and the MSVC build (Windows):

  * every imported DLL is part of Windows: no C/C++ runtime, libpng, zlib,
    giflib or libgeist DLL may be needed beside the executable;
  * an RT_MANIFEST resource with id 1 is embedded, exactly one, asking for
    per-monitor (v2) DPI awareness, Common Controls 6, asInvoker and long
    paths;
  * the icon and version resources are present;
  * the licence notices (RCDATA 103), which Help > About shows, carry the
    Apache License and the Hershey fonts acknowledgement and, with
    --require-notices, the libpng, zlib and giflib licence texts;
    --print also prints the embedded manifest.

    python3 win32/verify_exe.py path/to/geist-boo-reader.exe [--print] [--require-notices]
"""
import re
import struct
import sys

SYSTEM = {
    "kernel32.dll", "user32.dll", "gdi32.dll", "advapi32.dll", "shell32.dll", "ole32.dll",
    "oleaut32.dll", "comctl32.dll", "comdlg32.dll", "shlwapi.dll", "urlmon.dll", "uxtheme.dll",
    "msvcrt.dll", "ws2_32.dll", "bcrypt.dll", "ntdll.dll", "version.dll", "winspool.drv",
    "imm32.dll", "crypt32.dll", "secur32.dll",
}
RT_ICON, RT_RCDATA, RT_GROUP_ICON, RT_VERSION, RT_MANIFEST = 3, 10, 14, 16, 24
NOTICES = 103


class Pe:
    def __init__(self, data: bytes):
        self.data = data
        if data[:2] != b"MZ":
            raise SystemExit("not an MZ executable")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            raise SystemExit("not a PE executable")
        sections, optional_size = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
        optional = pe + 24
        magic = struct.unpack_from("<H", data, optional)[0]
        directories = optional + (112 if magic == 0x20B else 96)
        self.directory = [struct.unpack_from("<II", data, directories + 8 * i) for i in range(16)]
        self.sections = []
        table = optional + optional_size
        for i in range(sections):
            entry = table + 40 * i
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, entry + 8)
            self.sections.append((virtual_address, max(virtual_size, raw_size), raw_pointer))
        self.subsystem = struct.unpack_from("<H", data, optional + 68)[0]

    def offset(self, rva: int) -> int:
        for address, size, pointer in self.sections:
            if address <= rva < address + size:
                return pointer + rva - address
        raise SystemExit(f"RVA {rva:#x} is outside every section")

    def string(self, rva: int) -> str:
        start = self.offset(rva)
        return self.data[start:self.data.index(b"\0", start)].decode("ascii")

    def imports(self):
        rva, _ = self.directory[1]
        names, entry = [], self.offset(rva) if rva else None
        while entry is not None:
            name = struct.unpack_from("<5I", self.data, entry)[3]
            if not name:
                break
            names.append(self.string(name))
            entry += 20
        return names

    def resources(self):
        """{(type, id): [data bytes, ...]} for the three-level resource tree."""
        rva, _ = self.directory[2]
        if not rva:
            return {}
        base = self.offset(rva)

        def entries(offset):
            named, numbered = struct.unpack_from("<HH", self.data, offset + 12)
            for i in range(named + numbered):
                name, target = struct.unpack_from("<II", self.data, offset + 16 + 8 * i)
                yield name, target

        found = {}
        for type_id, type_target in entries(base):
            for name_id, name_target in entries(base + (type_target & 0x7FFFFFFF)):
                for _, leaf in entries(base + (name_target & 0x7FFFFFFF)):
                    data_rva, size = struct.unpack_from("<II", self.data, base + leaf)
                    start = self.offset(data_rva)
                    found.setdefault((type_id, name_id), []).append(self.data[start:start + size])
        return found


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    with open(path, "rb") as stream:
        pe = Pe(stream.read())
    problems = []
    imports = pe.imports()
    for name in imports:
        lower = name.lower()
        if lower not in SYSTEM and not lower.startswith("api-ms-win-"):
            problems.append(f"imports non-system DLL {name}")
    if pe.subsystem != 2:
        problems.append(f"subsystem {pe.subsystem} is not the Windows GUI subsystem")
    resources = pe.resources()
    manifests = [key for key in resources if key[0] == RT_MANIFEST]
    manifest = b""
    if manifests != [(RT_MANIFEST, 1)] or len(resources[(RT_MANIFEST, 1)]) != 1:
        problems.append(f"expected exactly one RT_MANIFEST resource with id 1, found {manifests}")
    else:
        manifest = resources[(RT_MANIFEST, 1)][0]
        text = manifest.decode("utf-8", "replace")
        for needle in (r"<dpiAwareness[^>]*>\s*PerMonitorV2, PerMonitor\s*<",
                       r"<dpiAware[^>]*>\s*true/pm\s*<",
                       r'name="Microsoft\.Windows\.Common-Controls"[^>]*version="6\.0\.0\.0"|'
                       r'version="6\.0\.0\.0"[^>]*name="Microsoft\.Windows\.Common-Controls"',
                       r'level="asInvoker"',
                       r"<longPathAware[^>]*>\s*true\s*<",
                       r"8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a",
                       r"35138b9a-5d96-4fbd-8e2d-a2440225f93a"):
            if not re.search(needle, text):
                problems.append(f"manifest lacks {needle}")
    for kind, label in ((RT_GROUP_ICON, "icon"), (RT_VERSION, "version")):
        if not any(key[0] == kind for key in resources):
            problems.append(f"no {label} resource")
    notices = b"".join(resources.get((RT_RCDATA, NOTICES), []))
    if b"Apache License" not in notices:
        problems.append("no licence notices resource carrying the Apache License")
    if b"=== Hershey fonts ===" not in notices or b"Dr. A. V. Hershey" not in notices:
        problems.append("licence notices lack the Hershey fonts acknowledgement")
    if "--require-notices" in sys.argv:
        for component in ("libpng", "zlib", "giflib"):
            if f"=== {component} ===".encode() not in notices:
                problems.append(f"licence notices lack the {component} licence")
        if b"Licence texts missing" in notices:
            problems.append("licence notices report missing texts")
    print(f"{path}")
    print("imports: " + ", ".join(imports))
    print(f"manifest: RT_MANIFEST/1, {len(manifest)} bytes")
    print(f"notices: RCDATA/{NOTICES}, {len(notices)} bytes")
    if "--print" in sys.argv and manifest:
        print(manifest.decode("utf-8", "replace"))
    for problem in problems:
        print(f"error: {problem}")
    print("ok" if not problems else f"{len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
