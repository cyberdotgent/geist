# Hosted BookServer Cache

This directory caches hosted BookServer HTML pages used for local-vs-hosted
rendering comparisons.

The cached pages were fetched from:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/<book>/<topic>?DT=<timestamp>&SHELF=
```

The first pass cached the first 20 TOC topics for local BOO fixtures rendered
with `boo2git --force` into `render/<book-stem>`. For complete-book audits, use
`tools/bookserver_book_audit.py`; it inventories every TOC topic, caches hosted
HTML, renders the matching local Markdown, and writes a TSV comparison manifest.
Keep exploratory complete-book output outside the repository unless it is
intentionally selected as durable format evidence.

Some directories contain BookServer message pages rather than topic bodies
because the hosted catalog did not contain a book matching the local BOO
basename. Keep those pages: they document failed mappings and prevent repeating
the same lookup.
