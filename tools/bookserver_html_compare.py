#!/usr/bin/env python3
"""Normalize BookServer HTML and compare it with local Markdown output."""

from __future__ import annotations

import argparse
import difflib
import html
from html.parser import HTMLParser
import re
import sys
import urllib.request


FOOTER_PATTERN = re.compile(
    r"<hr>\s*<br>\s*<a href=\"[^\"]*\">\s*<img src=\"/bookmgr/(?:prev|next)\.gif",
    re.IGNORECASE,
)


def strip_bookserver_footer(source: str) -> str:
    """Cut the hosted page at the prev/next navigation that ends the body.

    The body is delimited by bare <hr> tags, but BookServer also emits
    decorative rules inside the body: catalog separators wrapped as
    <a name="MSG ..."><hr></a> and attributed letter rules such as
    <hr size=4 width=100>. Only the bare <hr> before the navigation ends the
    topic.
    """
    match = FOOTER_PATTERN.search(source)
    return source[: match.start()] if match else source


def normalize_inline(value: str) -> str:
    value = html.unescape(value)
    value = value.replace("\xa0", " ")
    value = re.sub(r"\s+", " ", value).strip()
    value = re.sub(r"\s+([,.;:!?])", r"\1", value)
    value = re.sub(r"(\*{1,3})\s+", r"\1", value)
    value = re.sub(r"\s+(\*{1,3})", r"\1", value)
    # BookServer emphasizes word by word (<B>a</B> <B>b</B>) while Markdown
    # styles one run; compare the content, not the span boundaries.
    value = value.replace("****", " ").replace("**", "").replace("*", "")
    # Label/body spacing differs between <B>Label:</B> body and Markdown.
    value = re.sub(r":\s+", ":", value)
    return value


class BookServerParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.in_content = False
        self.done = False
        self.block_kind: str | None = None
        self.heading_level = 0
        self.buffer: list[str] = []
        self.blocks: list[str] = []
        self.marker_stack: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        tag = tag.lower()
        if self.done:
            return
        if tag == "hr":
            # The footer is stripped before parsing; the first <hr> opens the
            # body and later rules are decorative separators.
            if self.in_content:
                self.flush()
            else:
                self.in_content = True
            return
        if not self.in_content:
            return
        if tag in {"h1", "h2", "h3", "h4", "h5", "h6"}:
            self.flush()
            self.block_kind = "heading"
            self.heading_level = int(tag[1])
            self.buffer = []
            return
        if tag == "p":
            self.flush()
            self.block_kind = "paragraph"
            self.buffer = []
            return
        if tag == "br":
            self.buffer.append(" ")
            return
        if tag in {"b", "strong"}:
            self.buffer.append("**")
            self.marker_stack.append("**")
            return
        if tag in {"i", "em"}:
            self.buffer.append("*")
            self.marker_stack.append("*")
            return

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        if not self.in_content or self.done:
            return
        if tag in {"h1", "h2", "h3", "h4", "h5", "h6", "p"}:
            self.flush()
            return
        if tag in {"b", "strong", "i", "em"} and self.marker_stack:
            self.buffer.append(self.marker_stack.pop())

    def handle_data(self, data: str) -> None:
        if not self.in_content or self.done:
            return
        if self.block_kind is None:
            if not data.strip():
                return
            self.block_kind = "paragraph"
        self.buffer.append(data)

    def flush(self) -> None:
        if self.block_kind is None:
            self.buffer = []
            self.marker_stack = []
            return
        value = normalize_inline("".join(self.buffer))
        if value:
            if self.block_kind == "heading":
                self.blocks.append(f"H{self.heading_level}: {value}")
            else:
                self.blocks.append(f"P: {value}")
        self.block_kind = None
        self.heading_level = 0
        self.buffer = []
        self.marker_stack = []


ANCHOR_ONLY = re.compile(r"^(?:<a id=\"[^\"]*\"></a>\s*)+$")


def merge_label_blocks(blocks: list[str]) -> list[str]:
    """Remove anchor-only blocks and attach bare label blocks to their body.

    BookServer sometimes emits a section label such as "Meaning:" as its own
    block and sometimes inline with the body; Markdown anchors have no hosted
    counterpart. Neither difference is content.
    """
    result: list[str] = []
    pending: str | None = None
    for block in blocks:
        kind, _, text = block.partition(": ")
        if kind == "P" and ANCHOR_ONLY.match(text):
            continue
        if pending is not None:
            if kind == "P":
                block = f"P: {pending}{text}"
            else:
                result.append(f"P: {pending}")
            pending = None
        if kind == "P" and text.endswith(":") and len(text.split()) <= 3:
            pending = text
            continue
        result.append(block)
    if pending is not None:
        result.append(f"P: {pending}")
    return result


def normalize_bookserver_html(source: str) -> list[str]:
    parser = BookServerParser()
    parser.feed(strip_bookserver_footer(source))
    parser.flush()
    return merge_label_blocks(parser.blocks)


def normalize_markdown_heading(text: str) -> str:
    words = text.split()
    if len(words) > 1 and re.fullmatch(r"[A-Z0-9_.-]+", words[0]):
        return " ".join(words[1:])
    return text


def normalize_markdown(source: str) -> list[str]:
    blocks: list[str] = []
    paragraph: list[str] = []

    def flush_paragraph() -> None:
        if not paragraph:
            return
        value = " ".join(paragraph)
        # Markdown-only syntax: backslash escapes, list markers, and the
        # em-dash used to join definition-list terms to their descriptions.
        value = re.sub(r"\\(.)", r"\1", value)
        value = re.sub(r"(^|\s)[-*]\s+", r"\1", value)
        value = value.replace("\u2014", " ")
        value = normalize_inline(value)
        if value:
            blocks.append(f"P: {value}")
        paragraph.clear()

    for raw_line in source.splitlines():
        line = raw_line.strip()
        if not line:
            flush_paragraph()
            continue
        if line == "---" or line.startswith("[Previous]"):
            flush_paragraph()
            continue
        heading = re.match(r"^(#{1,6})\s+(.*)$", line)
        if heading:
            flush_paragraph()
            level = len(heading.group(1))
            text = normalize_markdown_heading(normalize_inline(heading.group(2)))
            if text:
                blocks.append(f"H{level}: {text}")
            continue
        paragraph.append(line)

    flush_paragraph()
    return merge_label_blocks(blocks)



def fetch_url(url: str, timeout: float) -> str:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "libgeist-bookserver-html-compare/1.0"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        charset = response.headers.get_content_charset() or "latin-1"
        return response.read().decode(charset, errors="replace")


def read_text(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Normalize hosted BookServer chapter HTML and optionally "
        "diff it against local Markdown output."
    )
    parser.add_argument("url", help="BookServer chapter URL")
    parser.add_argument(
        "-m",
        "--markdown",
        help="local Markdown file to normalize and compare",
    )
    parser.add_argument(
        "--raw-html",
        help="read BookServer HTML from a file instead of fetching the URL",
    )
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument(
        "--dump",
        action="store_true",
        help="print normalized BookServer blocks before any comparison",
    )
    args = parser.parse_args(argv)

    html_source = read_text(args.raw_html) if args.raw_html else fetch_url(
        args.url, args.timeout
    )
    bookserver_blocks = normalize_bookserver_html(html_source)

    if args.dump or not args.markdown:
        for block in bookserver_blocks:
            print(block)

    if not args.markdown:
        return 0

    markdown_blocks = normalize_markdown(read_text(args.markdown))
    if bookserver_blocks == markdown_blocks:
        print("normalized HTML and Markdown match")
        return 0

    diff = difflib.unified_diff(
        bookserver_blocks,
        markdown_blocks,
        fromfile="bookserver",
        tofile=args.markdown,
        lineterm="",
    )
    for line in diff:
        print(line)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
