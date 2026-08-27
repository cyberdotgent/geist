#!/usr/bin/env python3
"""Create a reproducible local-vs-BookServer rendering audit for one BOO."""

from __future__ import annotations

import argparse
import concurrent.futures
import csv
import difflib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

from bookserver_html_compare import (
    normalize_bookserver_html,
    normalize_markdown,
    strip_bookserver_footer,
)


class StructureParser(HTMLParser):
    """Count semantic elements only inside the BookServer topic body."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.in_content = False
        self.done = False
        self.counts = {
            "headings": 0,
            "pre": 0,
            "tables": 0,
            "images": 0,
            "links": 0,
            "lists": 0,
            "emphasis": 0,
        }

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        tag = tag.lower()
        if tag == "hr":
            # The footer is stripped before parsing; the first <hr> opens the
            # body and later rules are decorative separators.
            if not self.in_content:
                self.in_content = True
            return
        if not self.in_content or self.done:
            return
        if re.fullmatch(r"h[1-6]", tag):
            self.counts["headings"] += 1
        elif tag == "pre":
            self.counts["pre"] += 1
        elif tag == "table":
            self.counts["tables"] += 1
        elif tag == "img":
            self.counts["images"] += 1
        elif tag == "a":
            self.counts["links"] += 1
        elif tag in {"ul", "ol", "dl"}:
            self.counts["lists"] += 1
        elif tag in {"b", "strong", "i", "em"}:
            self.counts["emphasis"] += 1


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fetch every BookServer topic, render its local counterpart, "
        "and write a machine-readable comparison manifest."
    )
    parser.add_argument("boo", type=Path, help="local BOO fixture")
    parser.add_argument("--book-id", required=True, help="BookServer BOOKS identifier")
    parser.add_argument("--timestamp", required=True, help="BookServer DT value")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument(
        "--base-url",
        default="http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS",
    )
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--no-fetch",
        action="store_true",
        help="reuse output/reference/*.html without network requests",
    )
    parser.add_argument(
        "--refresh",
        action="store_true",
        help="replace reference HTML already present in the output directory",
    )
    return parser.parse_args(argv)


def executable(build_dir: Path, name: str) -> Path:
    candidates = [build_dir / name, build_dir / "Debug" / f"{name}.exe"]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(f"could not find {name} under {build_dir}")


def run(command: list[str]) -> str:
    completed = subprocess.run(
        command,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return completed.stdout


def parse_toc(output: str) -> list[dict[str, object]]:
    topics: list[dict[str, object]] = []
    pattern = re.compile(r"^( *)([^\t]+)\t(.*?)\tstyle (\d+)$")
    for line in output.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        topics.append(
            {
                "ordinal": len(topics) + 1,
                "level": len(match.group(1)) // 2,
                "id": match.group(2),
                "title": match.group(3),
                "style": int(match.group(4)),
            }
        )
    if not topics:
        raise RuntimeError("bootoc returned no parseable topics")
    return topics


def slug(ordinal: int, topic_id: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", topic_id).strip("._") or "topic"
    return f"{ordinal:04d}-{safe}"


def topic_url(args: argparse.Namespace, topic_id: str) -> str:
    path = "/".join(
        urllib.parse.quote(part, safe=".-_") for part in (args.book_id, topic_id)
    )
    query = urllib.parse.urlencode({"DT": args.timestamp, "SHELF": ""})
    return f"{args.base_url.rstrip('/')}/{path}?{query}"


def fetch(url: str, timeout: float) -> bytes:
    request = urllib.request.Request(
        url, headers={"User-Agent": "libgeist-book-audit/1.0"}
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def bookserver_error(body: bytes) -> str:
    lowered = body.lower()
    known_errors = (
        b"book could not be located",
        b"topic could not be located",
        b"requested topic was not found",
    )
    if any(message in lowered for message in known_errors):
        return "BookServer returned an HTML error page"
    return ""


def fetch_one(
    args: argparse.Namespace, topic: dict[str, object], reference_dir: Path
) -> tuple[str, str]:
    target = reference_dir / f"{slug(int(topic['ordinal']), str(topic['id']))}.html"
    url = topic_url(args, str(topic["id"]))
    if target.exists() and not args.refresh:
        error = bookserver_error(target.read_bytes())
        if error:
            return "server-error", error
        return "cached", ""
    if args.no_fetch:
        return "missing", "reference file is not cached"
    try:
        body = fetch(url, args.timeout)
        target.write_bytes(body)
        error = bookserver_error(body)
        if error:
            return "server-error", error
        return "fetched", ""
    except (OSError, urllib.error.URLError) as error:
        return "error", str(error).replace("\t", " ").replace("\n", " ")


def markdown_structure(source: str) -> dict[str, int]:
    return {
        "headings": len(re.findall(r"(?m)^#{1,6}\s+", source)),
        "pre": len(re.findall(r"(?m)^```", source)) // 2,
        "tables": len(re.findall(r"(?m)^\|(?:[^\n]*\|)+\s*$", source)),
        "images": len(re.findall(r"!\[[^]]*\]\([^)]+\)", source)),
        "links": len(re.findall(r"(?<!!)\[[^]]+\]\([^)]+\)", source)),
        "lists": len(re.findall(r"(?m)^(?:[-*+] |\d+\. )", source)),
        "emphasis": len(re.findall(r"\*\*[^*]+\*\*|(?<!\*)\*[^*]+\*(?!\*)", source)),
    }


def reference_structure(source: str) -> dict[str, int]:
    parser = StructureParser()
    parser.feed(strip_bookserver_footer(source))
    return parser.counts


def normalized_text(blocks: list[str]) -> str:
    return "\n".join(re.sub(r"^[HP]\d?:\s*", "", block) for block in blocks)


def flags_for(
    markdown: str,
    reference: str,
    ratio: float,
    local: dict[str, int],
    remote: dict[str, int],
) -> list[str]:
    flags: list[str] = []
    if not markdown.strip():
        flags.append("empty-local")
    if not reference.strip():
        flags.append("empty-reference")
    if "<geist-placeholder" in markdown:
        flags.append("decoder-placeholder")
    if "\ufffd" in markdown:
        flags.append("invalid-utf8")
    if ratio < 0.50:
        flags.append("low-text-match")
    elif ratio < 0.80:
        flags.append("medium-text-match")
    for key in ("tables", "images", "links", "lists", "emphasis"):
        if remote[key] and not local[key]:
            flags.append(f"missing-{key}")
    return flags


def write_tsv(path: Path, fieldnames: list[str], rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t")
        writer.writeheader()
        writer.writerows(rows)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    args.output.mkdir(parents=True, exist_ok=True)
    local_dir = args.output / "local"
    reference_dir = args.output / "reference"
    diff_dir = args.output / "diff"
    for directory in (local_dir, reference_dir, diff_dir):
        directory.mkdir(exist_ok=True)

    bootoc = executable(args.build_dir, "bootoc")
    boorender = executable(args.build_dir, "boorender")
    boo = args.boo.resolve()
    topics = parse_toc(run([str(bootoc), str(boo)]))

    fetch_results: dict[int, tuple[str, str]] = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {
            pool.submit(fetch_one, args, topic, reference_dir): int(topic["ordinal"])
            for topic in topics
        }
        for future in concurrent.futures.as_completed(futures):
            fetch_results[futures[future]] = future.result()

    rows: list[dict[str, object]] = []
    for topic in topics:
        ordinal = int(topic["ordinal"])
        topic_id = str(topic["id"])
        stem = slug(ordinal, topic_id)
        markdown_path = local_dir / f"{stem}.md"
        reference_path = reference_dir / f"{stem}.html"
        diff_path = diff_dir / f"{stem}.diff"
        markdown = run([str(boorender), str(boo), topic_id, "--md"])
        markdown_path.write_text(markdown, encoding="utf-8")
        fetch_status, fetch_error = fetch_results[ordinal]
        row: dict[str, object] = {
            **topic,
            "url": topic_url(args, topic_id),
            "fetch_status": fetch_status,
            "fetch_error": fetch_error,
            "text_ratio": "",
            "flags": (
                "fetch-error"
                if fetch_status in {"error", "missing", "server-error"}
                else ""
            ),
            "local_path": str(markdown_path),
            "reference_path": str(reference_path),
            "diff_path": "",
        }
        if reference_path.exists():
            reference = reference_path.read_bytes().decode("latin-1", errors="replace")
            local_blocks = normalize_markdown(markdown)
            remote_blocks = normalize_bookserver_html(reference)
            local_text = normalized_text(local_blocks)
            remote_text = normalized_text(remote_blocks)
            ratio = difflib.SequenceMatcher(None, remote_text, local_text).ratio()
            local_counts = markdown_structure(markdown)
            remote_counts = reference_structure(reference)
            flags = flags_for(markdown, reference, ratio, local_counts, remote_counts)
            if fetch_status in {"error", "missing", "server-error"}:
                flags.insert(0, "fetch-error")
            diff = "\n".join(
                difflib.unified_diff(
                    remote_blocks,
                    local_blocks,
                    fromfile=f"BookServer/{topic_id}",
                    tofile=f"libgeist/{topic_id}",
                    lineterm="",
                )
            )
            diff_path.write_text(diff + ("\n" if diff else ""), encoding="utf-8")
            row.update(
                {
                    "text_ratio": f"{ratio:.4f}",
                    "flags": ",".join(flags),
                    "local_structure": json.dumps(local_counts, sort_keys=True),
                    "reference_structure": json.dumps(remote_counts, sort_keys=True),
                    "diff_path": str(diff_path),
                }
            )
        else:
            row.update({"local_structure": "", "reference_structure": ""})
        rows.append(row)

    fields = [
        "ordinal", "level", "id", "title", "style", "url", "fetch_status",
        "fetch_error", "text_ratio", "flags", "local_structure",
        "reference_structure", "local_path", "reference_path", "diff_path",
    ]
    write_tsv(args.output / "manifest.tsv", fields, rows)
    metadata = {
        "boo": str(boo),
        "book_id": args.book_id,
        "timestamp": args.timestamp,
        "base_url": args.base_url,
        "topic_count": len(topics),
        "command": " ".join(sys.argv),
    }
    (args.output / "book.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    suspicious = sum(bool(row["flags"]) for row in rows)
    print(f"audited {len(rows)} topics; {suspicious} topics have heuristic flags")
    print(args.output / "manifest.tsv")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
