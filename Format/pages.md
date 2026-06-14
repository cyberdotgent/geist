# BOO Page Organization

This page is a stub for BOO page-level structures.

## Verified

- The two repository fixtures are exact multiples of 4096 bytes.
- Structures observed so far start on 4096-byte boundaries.
- Page 0 starts with a big-endian directory page number, not a normal page-class
  word. In both fixtures it is `0x0001`, pointing to the directory at page 1.
- For non-header content pages, the first two bytes behave like a page class or
  flags word.
- Page-1 directory fields identify the first `0x0100` page run and the
  following `0x0000` page run in both fixtures.

See [boo-header.md](boo-header.md) for the current evidence tables.

## Open Questions

- Meaning of page-class values `0x0100`, `0x0000`, `0x0001`, and `0x010b`.
- Whether page headers have a common structure after the first two bytes.
- How page runs map to document text, indexes, table of contents, and resources.
