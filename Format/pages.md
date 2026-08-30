# BOO Page Organization

A BOO file is a flat array of 4096-byte physical pages. This note documents how
a reader decides what each page is. The rule in one line: **page roles come
from the directory, never from the page's own bytes.**

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

## The Four Page Roles

Every page a reader needs is named by a directory field. Nothing else in the
file has to be visited.

| Role | Class word | Named by | Header | Contents |
| --- | --- | --- | --- | --- |
| File header | — | file offset `0` | none | Directory locator, copyright text, resource descriptors. See [boo-header.md](boo-header.md). |
| Directory | — | page-0 word `0x0000` | none | The fixed scalars and the four in-page indexes. |
| Dictionary | `0x0100` | directory `0x0028` (start), `0x002e` (count) | `class, used_length` | Token base and delta records. |
| Content | `0x0000` | directory `0x003a` (start), `0x0038` (count) | `class, used_length` | Topic logical records. |
| Token index | `0x0001` | the page table at directory `0x001a` | `class, used_length` | Search index; see below. |

Pages not named by any of these are either resource payloads (before the
directory page; see [assets.md](assets.md)) or continuation pages of a chained
directory index.

### The Structural Page Header Is Four Bytes

All three structural classes share one header, and there is no fourth class:

```c
struct BooStructuralPage {
  uint16_t page_class_be;    // 0x0000, 0x0001 or 0x0100.
  uint16_t used_length_be;   // Byte offset one past the last used byte.
  uint8_t  records[];        // Compact-length-prefixed records, from offset 4.
};
```

`used_length` is a page-relative byte offset and is never more than `0x1000`. A
reader parses records from offset `4` up to `min(used_length, 4096)` and stops;
the tail is stale bytes, not padding to be decoded. `QS3X36CM.BOO` shows the
whole range: dictionary page 2 declares `0x0fd8`, content page 26 — the last
one, holding the tail of the last topic — declares `0x0047`, and token-index
page 27 declares the full `0x1000`.

### A Continuation Page Has No Page Header

The four directory indexes (`0x0034` content-page records, `0x003c` topic
starts, `0x0040` stemming, and the `0x001a` token-index page table) are all
count-prefixed and all may continue onto another page. A continuation table
starts at **offset 0** of its page with its own `count, next` header and no
page-class word at all.

This is what the "isolated `0x010b` page class" in `OFCUSEOV.BOO` was.
Page 87 of that book begins `01 0b 00 00 e4 18 e4 15 e4 19 e4 15 ...` —
`0x010b` = 267 is a **count**, `0x0000` is "no further continuation", and the
534 bytes that follow are 267 four-byte stemming pairs. `OFCUSEOV.BOO`'s
directory `0x025c` root reads `018e 0057`: 398 entries here, continuation on
logical page `0x57` = 87. The two counts sum to 665 pairs.

`0x010b` is therefore not a page class and there is no fourth structural class.
Reading it as one is exactly the failure the first-word rule above warns about:
a continuation page's first word is whatever number the table needed.

### The Trailing `0x0001` Run Is The Token Index

The trailing run holds the book's search index, keyed by dictionary token. It
is reached only through the page table at directory offset `0x001a` (see
[boo-header.md](boo-header.md#the-token-index-page-table)), which gives, per
page, the first extended token key that page covers.

Verified over all 35 fixtures: the pages that table names are exactly the pages
of the trailing `0x0001` run, in order; the first key of the first page is
always `threshold * 256`, the lowest extended key a book can spell; and the
keys ascend across pages. In `QS3X36CM.BOO` the four pages 27..30 start at
`0xdc00`, `0xe299` (`determine`), `0xe877` (`parameter`) and `0xeec6`
(`wrkdtadct`) — the dictionary is alphabetical, so the index is too.

Within a page, entries use the same compact length prefix as every other BOO
record and follow one another from page offset `4`. Walking `QS3X36CM.BOO`
page 27 gives entries of 6, 19, 33, 3, 5, 5, 5, 5, 16, 2, 33, 33 bytes, and
the walk consumes the page cleanly. The payloads are occurrence sets in at
least two forms — short delta-style lists such as `01 03 01 02 01 01 01 01 7e
0e 02 01 09 02 0d 13 12 16 01`, and a 33-byte `01 ff` form whose 31 payload
bytes are a bitmap wide enough for the book's 241 logical records.

Nothing in topic rendering consults this run. A reader that renders topics can
stop at the content run; a reader that implements search needs the payload
encoding, which is the open question below.

### Why Both A Content Run And A Trailing Run Exist

They hold different things, not two copies of one thing. The `0x0000` content
run holds the book's text, addressed by logical record number through the
content-page record index at directory `0x0034`. The trailing `0x0001` run
holds the token index, addressed by token key through the page table at
directory `0x001a`. Neither index names a page in the other's run in any of the
35 fixtures, and the entry counts differ in kind: `QS3X36CM.BOO`'s content run
is indexed by its 241 logical records, its token index by its 5,113 dictionary
tokens.

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

- The per-token payload of a token-index entry. The page table, the page
  ordering, the per-page first key and the compact-length entry framing are all
  verified above; what a payload's bytes mean is not. Two forms are visible in
  `QS3X36CM.BOO` page 27 — a short list form and a 33-byte `01 ff` bitmap form
  — and the number of entries in the run does not equal the book's token count
  (3,375 entries against 6,429 tokens in `packet.boo`, measured by walking the
  compact-length chain of every trailing page in all 35 fixtures), so entries
  are not one per token. Settling it needs a hosted BookServer search whose
  result set can be compared against a decoded payload.
- Whether the individual bits of the class words `0x0000`, `0x0001` and
  `0x0100` mean anything. Only these three values occur in a declared run
  across all 35 fixtures, so the corpus offers no differential evidence; the
  values are usable as tags whether or not they decompose.
