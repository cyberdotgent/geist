# BOO Page Organization

This page is a stub for BOO page-level structures.

## Verified

- Every one of the 34 `BOO/` fixtures is an exact multiple of 4096 bytes
  (checked mechanically over all 34; no remainder in any file).
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
- For pages the directory's page runs declare as structural, the first two bytes
  are a page class word. Only three class values occur in that role across the
  whole corpus.
- Page-1 directory fields identify the first `0x0100` page run and the
  following `0x0000` page run in both original fixtures; in shifted books these
  same logical runs start at `directory_page + logical_start - 1`.

### The First Word Is A Class Word Only In Declared Structural Runs

Reading the first big-endian word of every page of every fixture (34 files,
5,966 pages after page 0) yields three recurring values and a long tail of
one-off values:

| First word | Pages | Books | Role |
| ---: | ---: | ---: | --- |
| `0x0000` | 2,742 | 34 | Content (topic logical-record) pages. |
| `0x0001` | 450 | 34 | Trailing logical-record pages. |
| `0x0100` | 307 | 34 | Dictionary/token pages. |
| everything else | 2,467 | 32 | Not a class word. |

The tail is not a class vocabulary: the values are image bytes. `0x9249`,
`0x4924` and `0x2492` (122/118/117 pages) are the repeating bit pattern of a
1-bit-per-pixel raster; `0x421b`, `0x421c`, `0x421d`, `0x421e`, `0x421f`,
`0x4220`, `0x4222`, ... are the first bytes of legacy image payloads; `0xffff`
appears in 37 pages of 9 books. These pages lie inside the embedded resource
region described in [assets.md](assets.md), not inside any directory-declared
page run.

An independent reader must therefore take page roles from the directory's page
runs and must never classify a page by its first word alone. Classifying by
first word is what makes a decoder append fabricated records to the last topic;
see the `GG24-4302-00.boo` case in
[table-of-contents.md](table-of-contents.md#content-page-record-index).

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

- The class words `0x0000`, `0x0001` and `0x0100` are identified by role
  (content, trailing logical records, dictionary). What the individual bits of
  the word mean, and whether `0x010b` is a fourth structural class or a
  resource-payload byte pair, is unresolved; `0x010b` does not occur as the
  first word of any page in the 34 bundled fixtures.
- Whether page headers have a common structure after the first two bytes. Only
  `0x0000`/`0x0001`/`0x0100` pages are known to carry a used-length word at
  offset `0x0002`.
- Why both a `0x0000` content run and a trailing `0x0001` logical-record run
  exist, and what the trailing run holds that the content run does not.
