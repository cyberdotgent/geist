#!/usr/bin/env python3
# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0
"""Regenerates win32/geist-boo-reader.ico from gui/geist-hardcopy.svg.

The Win32 reader reuses the icon the macOS reader is built from. The CI
Windows runner has no SVG rasteriser, so the .ico is committed; it is made by
this script, which reads the SVG and draws the small subset it uses (a
rounded rect and stroked paths of M/L/H/V/A/Z segments with round joins and caps, with presentation
attributes inherited from <g>) with Pillow at 1024 px, then reduces it to each
size and packs the sizes as PNG-compressed entries (Windows Vista and later):

    python3 win32/make_icon.py

Run it again whenever the SVG changes and commit the result. It fails on any
SVG construct it does not understand rather than drawing something else.
"""
import io
import math
import pathlib
import re
import struct
import xml.etree.ElementTree as ElementTree

from PIL import Image, ImageDraw

SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
CANVAS = 1024
ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "gui" / "geist-hardcopy.svg"
TARGET = ROOT / "win32" / "geist-boo-reader.ico"
NS = "{http://www.w3.org/2000/svg}"


def arc_points(start, rx, ry, rotation, large, sweep, end, steps=24):
    """SVG endpoint arc to points (rotation must be 0 here)."""
    if rotation:
        raise SystemExit("rotated arcs are not supported")
    (x1, y1), (x2, y2) = start, end
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    rx, ry = abs(rx), abs(ry)
    scale = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry)
    if scale > 1:
        rx, ry = rx * math.sqrt(scale), ry * math.sqrt(scale)
    numerator = rx * rx * ry * ry - rx * rx * dy * dy - ry * ry * dx * dx
    factor = math.sqrt(max(0.0, numerator / (rx * rx * dy * dy + ry * ry * dx * dx)))
    if large == sweep:
        factor = -factor
    cx = factor * rx * dy / ry + (x1 + x2) / 2
    cy = -factor * ry * dx / rx + (y1 + y2) / 2
    a1 = math.atan2((y1 - cy) / ry, (x1 - cx) / rx)
    a2 = math.atan2((y2 - cy) / ry, (x2 - cx) / rx)
    delta = a2 - a1
    if sweep and delta < 0:
        delta += 2 * math.pi
    if not sweep and delta > 0:
        delta -= 2 * math.pi
    return [(cx + rx * math.cos(a1 + delta * i / steps), cy + ry * math.sin(a1 + delta * i / steps))
            for i in range(1, steps + 1)]


def subpaths(d):
    tokens = re.findall(r"[MmLlHhVvAaZz]|-?\d*\.?\d+(?:e-?\d+)?", d)
    result, current, point, start, index, command = [], [], (0.0, 0.0), (0.0, 0.0), 0, None

    def number():
        nonlocal index
        value = float(tokens[index])
        index += 1
        return value

    while index < len(tokens):
        if re.fullmatch(r"[A-Za-z]", tokens[index]):
            command = tokens[index]
            index += 1
        relative = command.islower()
        kind = command.upper()
        x0, y0 = point if relative else (0.0, 0.0)
        if kind == "M":
            if current:
                result.append((current, False))
            point = (x0 + number(), y0 + number())
            start, current = point, [point]
            command = "l" if relative else "L"
        elif kind == "L":
            point = (x0 + number(), y0 + number())
            current.append(point)
        elif kind == "H":
            point = ((point[0] if relative else 0.0) + number(), point[1])
            current.append(point)
        elif kind == "V":
            point = (point[0], (point[1] if relative else 0.0) + number())
            current.append(point)
        elif kind == "A":
            rx, ry, rotation, large, sweep = number(), number(), number(), number(), number()
            end = (x0 + number(), y0 + number())
            current += arc_points(point, rx, ry, rotation, bool(large), bool(sweep), end)
            point = end
        elif kind == "Z":
            result.append((current, True))
            current, point = [], start
        else:
            raise SystemExit(f"unsupported path command {command}")
    if current:
        result.append((current, False))
    return result


def colour(value):
    if not re.fullmatch(r"#[0-9a-fA-F]{6}", value):
        raise SystemExit(f"unsupported colour {value}")
    return tuple(int(value[i:i + 2], 16) for i in (1, 3, 5)) + (255,)


def draw(element, image, factor, inherited):
    attributes = dict(inherited)
    attributes.update(element.attrib)
    tag = element.tag.replace(NS, "")
    pen = ImageDraw.Draw(image)
    if tag == "rect":
        box = [0, 0, float(attributes["width"]) * factor - 1, float(attributes["height"]) * factor - 1]
        pen.rounded_rectangle(box, radius=float(attributes.get("rx", 0)) * factor, fill=colour(attributes["fill"]))
    elif tag == "path":
        if attributes.get("fill", "none") != "none":
            raise SystemExit("filled paths are not supported")
        width = round(float(attributes["stroke-width"]) * factor)
        ink = colour(attributes["stroke"])
        for points, closed in subpaths(attributes["d"]):
            scaled = [(x * factor, y * factor) for x, y in points]
            if closed:
                scaled.append(scaled[0])
            # A round pen stamped every pixel along the outline: round
            # joins and caps, as stroke-linejoin/linecap="round" ask.
            radius = width / 2
            for (xa, ya), (xb, yb) in zip(scaled, scaled[1:] or scaled):
                steps = max(1, int(math.hypot(xb - xa, yb - ya)))
                for i in range(steps + 1):
                    x, y = xa + (xb - xa) * i / steps, ya + (yb - ya) * i / steps
                    pen.ellipse([x - radius, y - radius, x + radius, y + radius], fill=ink)
    elif tag in ("g", "svg"):
        pass
    else:
        raise SystemExit(f"unsupported element {tag}")
    for child in element:
        draw(child, image, factor, {k: v for k, v in attributes.items() if k not in ("d", "width", "height", "rx")})


def main() -> None:
    svg = ElementTree.parse(SOURCE).getroot()
    if svg.get("viewBox") != "0 0 64 64":
        raise SystemExit("expected a 64x64 viewBox")
    image = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw(svg, image, CANVAS / 64, {})
    images = []
    for size in SIZES:
        buffer = io.BytesIO()
        image.resize((size, size), Image.LANCZOS).save(buffer, "PNG", optimize=True)
        images.append(buffer.getvalue())
    header = struct.pack("<HHH", 0, 1, len(SIZES))
    offset = len(header) + 16 * len(SIZES)
    entries, data = b"", b""
    for size, png in zip(SIZES, images):
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset)
        offset += len(png)
        data += png
    TARGET.write_bytes(header + entries + data)
    print(f"{TARGET}: {len(SIZES)} sizes, {TARGET.stat().st_size} bytes")


if __name__ == "__main__":
    main()
