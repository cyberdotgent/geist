# BOO Page Organization

This page is a stub for BOO page-level structures.

## Verified

- The two repository fixtures are exact multiples of 4096 bytes.
- Structures observed so far start on 4096-byte boundaries.
- Page 0 starts with a big-endian directory page number, not a normal page-class
  word. In the original two fixtures it is `0x0001`, pointing to the directory
  at physical page 1. Later fixtures verify that this can point farther into
  the file, for example `SC09-2417-00.boo` uses directory page `0x002b`.
- Directory-page fields that name pages are 1-based logical page numbers. A
  logical page maps to a physical file page with:

  ```text
  physical_page = directory_page + logical_page - 1
  byte_offset = physical_page * 4096
  ```

  Therefore logical page `1` is the directory page itself. For shifted books,
  bytes before the directory page are a physical prelude and are not counted by
  directory offset `0x0016`.
- For non-header content pages, the first two bytes behave like a page class or
  flags word.
- Page-1 directory fields identify the first `0x0100` page run and the
  following `0x0000` page run in both original fixtures; in shifted books these
  same logical runs start at `directory_page + logical_start - 1`.

## Shifted Directory Evidence

These fixtures failed when `libgeist` treated directory page fields as absolute
physical page numbers. They validate the logical-page-base rule above:

| File | File pages | Directory page from page 0 | Directory `0x0016` | Physical last page calculation |
| --- | ---: | ---: | ---: | ---: |
| `GX27-3999-00.boo` | 27 | 6 | 21 | `6 + 21 - 1 = 26` |
| `SC09-2417-00.boo` | 190 | 43 | 147 | `43 + 147 - 1 = 189` |
| `SC28-1881-05.boo` | 169 | 35 | 134 | `35 + 134 - 1 = 168` |
| `SC24-5527-02.boo` | 113 | 15 | 98 | `15 + 98 - 1 = 112` |

In each case the physical last page calculation equals `file_page_count - 1`.

See [boo-header.md](boo-header.md) for the current evidence tables.

## Open Questions

- Meaning of page-class values `0x0100`, `0x0000`, `0x0001`, and `0x010b`.
- Whether page headers have a common structure after the first two bytes.
- How page runs map to document text, indexes, table of contents, and resources.
