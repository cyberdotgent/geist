# BOO Container And Book Header

This note describes the BOO file header structures that are currently verified
from the repository fixtures and, where noted, from rendered output of the
hosted BookServer service.

Primary fixtures:

- `BOO/QS3X36CM.BOO`, 126,976 bytes, 31 pages of 4096 bytes.
- `BOO/OFCUSEOV.BOO`, 405,504 bytes, 99 pages of 4096 bytes.

**The corpus.** Statements below that count "fixtures" count 35 books: the 34
`.BOO`/`.boo` files under `BOO/` plus `Official Readers/SoftCopy/HLCRUG21.boo`.
Where a claim was measured over only the 34 `BOO/` books this note says so.

Conventions used throughout:

- Pages are 4096 bytes, so a page number becomes a byte offset by multiplying
  by 4096 (equivalently, shifting left by 12).
- Multi-byte integer fields are big-endian. This is directly observable: page-0
  word `0x0000` reads as `0x0001` (`00 01`) in books whose directory is at
  physical page 1, and as `0x0034` (`00 34`) in `GG24-4302-00.boo`, whose
  directory really does begin at file offset `0x00034000`.
- EBCDIC text in the physical header bytes decodes as code page 037.

## Physical Layout

Both sample files are exact multiples of 4096 bytes. A BOO reader should treat
the file as a sequence of 4096-byte physical pages.

| File | Size | Physical pages |
| --- | ---: | ---: |
| `QS3X36CM.BOO` | `0x0001f000` / 126,976 | 31 |
| `OFCUSEOV.BOO` | `0x00063000` / 405,504 | 99 |

## Page 0: File Header And Directory Locator

Page 0 is not the main directory. The first two bytes are a big-endian physical
page number for the directory/header page. Multiply it by 4096 to get the byte
offset of the directory. In both primary fixtures the directory page number is
`0x0001`, so the directory begins at file offset `0x1000`.

| Absolute offset | Size | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Meaning |
| ---: | ---: | --- | --- | --- |
| `0x0000` | 2 | `00 01` | `00 01` | Big-endian directory page number. |
| `0x0002` | 2 | `00 00` | `00 00` | Unresolved; zero in all fixtures. |
| `0x0004` | 4 | `00 00 00 00` | `00 00 00 00` | Picture/object count for books with a pre-directory resource area; zero in books without one. See [assets.md](assets.md). |
| `0x0008` | 4 | `40 40 40 40` | `40 40 40 40` | EBCDIC spaces. |
| `0x000c` | 246 | `b4 40 c3 ... f1` | `b4 40 c3 ... f0` | EBCDIC copyright text, right-padded with `0x40` to `0x0101`. |
| `0x0102` | 4 | `df 70 2f 8c` | `3d 85 8f f6` | Optional 4-byte binary book stamp. See below. |
| `0x0106` | 2 | `40 40` | `40 40` | EBCDIC spaces; the tail of the fixed 250-byte text field. |
| `0x0108` | 8 | zeros | zeros | Zero in all 35 fixtures. |
| `0x0110` | 8 | zeros | zeros | Resource-descriptor area header; zero when the book has no pictures. See [assets.md](assets.md#page-0-resource-directory). |
| `0x0118` | variable | — | — | First resource-descriptor group, when the book has one. |

Decoded page-0 text:

| File | Text |
| --- | --- |
| `QS3X36CM.BOO` | `<copyright sign> Copyright IBM Corp. 1991` |
| `OFCUSEOV.BOO` | `<copyright sign> Copyright IBM Corp. 1988, 1990` |

### The Value At `0x0102` Sits Inside The Text Field

The bytes from `0x000c` to `0x0107` are one fixed 250-byte EBCDIC field:
`0x40` padding runs from the end of the copyright text all the way to `0x0107`
in every fixture, and `0x0108` begins the resource-descriptor area. Only the
four bytes at `0x0102..0x0105` break the padding, and they do not break it
everywhere: `packet.boo`, `XWEBDEMO.boo` and `HLCRUG21.boo` — the three books
built by the newest BookManager BUILD in the corpus — store `40 40 40 40`
there, so the field is optional and its unset value is EBCDIC spaces.

In the other 32 fixtures it holds four bytes that look random and are constant
per book. It is not derived from any of the quantities checked: file size, page
count, directory page number, logical-record count, topic count, dictionary
token count, the directory timestamp, or the length or content of the
copyright text. Settling it needs either two builds of the same book, or a
build whose input differs in one known respect; the corpus has neither. A
reader must not depend on this field, and must not treat it as text — decoding
the copyright string past `0x0101` yields garbage in 32 of 35 fixtures.

## Directory Page

The directory page starts with a 16-byte prefix, followed by fixed fields and
in-page offsets. In the two primary fixtures this page is physical page 1, so
the absolute file offset is `0x1000 + directory_offset`.

### The 16-Byte Prefix Is A Dialect Flag, Not Padding

The 16-byte prefix is zero except for the two bytes at directory offsets
`0x0009..0x000a`, which carry the picture-directory layout version. Across the
35 fixtures exactly three values occur:

| Bytes at `0x0009..0x000a` | Fixtures | Picture-directory layout |
| --- | ---: | --- |
| `00 00` | 28 | version 1.2, one descriptor group |
| `01 03` | 5 | version 1.3, two descriptor groups |
| `01 00` | 2 | version 1.4, three descriptor groups |

See [assets.md](assets.md) for the descriptor-group layouts these select. This
field is independent of the directory version text below, which is `" 1.2"` in
every fixture.

The same two bytes are the cheapest available test for the **body-markup
dialect**, before a single token is resolved. The six fixtures with a non-zero
byte at `0x0009` are exactly the six that store block boundaries as `CZ`
controls and exactly the six whose decoded header carries `CBLDVERS=1.3.0`; the
28 with `00 00` carry no `CZ` control on any display line. See
[markup.md](markup.md#cz-layout-directives). The correlation is exact over the
corpus but is a correlation, not a definition: a book could in principle carry
pictures in a 1.3 directory and no `CZ` markup, so a decoder that cares should
still fall back to scanning the body.

`SC41-485.boo` is the near-miss that shows the two roles are distinct: it
declares `01 03` yet stores **zero** picture descriptors, so it has a version-1.3
picture directory containing nothing, and it is a `CZ` book.

Directory offset `0x0010` holds a version string: bytes `40 f1 4b` (EBCDIC
`" 1."`) followed by an EBCDIC digit byte selecting the directory layout
variant. All 35 fixtures carry `f2`, EBCDIC `" 1.2"`.

### Fixed Directory Fields

The table below lists the fixed directory offsets whose storage role is
established. Names are descriptive where semantics are known and neutral where
only the storage role is verified. Values without an identified purpose are
marked as such, with the range observed across all 35 fixtures.

| Directory offset | Size | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Interpretation |
| ---: | ---: | ---: | ---: | --- |
| `0x0010` | 4 | EBCDIC `" 1.2"` | EBCDIC `" 1.2"` | Directory format/version text. |
| `0x0014` | 1 | `0xdc` | `0xd5` | Token threshold, as a byte. Identical to the word at `0x0024` in all 35 fixtures; either spelling may be read. |
| `0x0015` | 1 | `0xf0` | `0xf0` | EBCDIC `0`. Constant in all 35 fixtures. |
| `0x0016` | 2 | `0x001e` / 30 | `0x0062` / 98 | Highest valid 1-based logical page number. For books whose directory is at physical page 1 this equals `file_page_count - 1`; shifted-directory fixtures show it is relative to the directory page. |
| `0x001a` | 2 | `0x089c` | `0x089c` | In-page offset of the token-index page table. Constant `0x089c` because the directory-page region layout is fixed; the table it names is per-book. See [The Token-Index Page Table](#the-token-index-page-table). |
| `0x0022` | 2 | `0x0c8c` | `0x0c8c` | In-page offset of the two-byte token map used for one-byte token IDs. The map holds `threshold` entries. |
| `0x0024` | 2 | `0x00dc` | `0x00d5` | Token threshold; bytes below this value are one-byte token IDs in logical records. |
| `0x0026` | 2 | `0x0e44` | `0x0e38` | In-page offset of the version-2 dictionary token-lookup root index. Observed at `0x0c8c + 2 * threshold` in 24 of 35 fixtures and two bytes past that in the other 11, so read the stored value rather than deriving it. |
| `0x0028` | 2 | `0x0002` / 2 | `0x0002` / 2 | Start logical page of the observed `0x0100` page run. |
| `0x002a` | 2 | `0x0af5` / 2805 | `0x1229` / 4649 | Count of dictionary tokens that add or change letters. See [The Two Dictionary Token Counts](#the-two-dictionary-token-counts). |
| `0x002c` | 2 | `0x13f9` / 5113 | `0x1a4c` / 6732 | Total extended dictionary token count. Extended token keys run from `threshold * 256` upward for exactly this many values. |
| `0x002e` | 2 | `0x0005` / 5 | `0x0008` / 8 | Count of pages in the `0x0100` page run. |
| `0x0034` | 2 | `0x0e82` | `0x0ed2` | In-page offset of the content-page logical-record index; resolve as `directory_base + value`. See [table-of-contents.md](table-of-contents.md). |
| `0x0036` | 2 | `0x00f1` | `0x03f5` | Total logical-record count. Matches the terminal value of the content-page record index in every fixture. |
| `0x0038` | 2 | `0x0014` / 20 | `0x004d` / 77 | Count of pages in the following `0x0000` page run. |
| `0x003a` | 2 | `0x0007` / 7 | `0x000a` / 10 | Start logical page of the following `0x0000` page run. |
| `0x003c` | 2 | `0x0068` / 104 | `0x0068` / 104 | In-page offset of the topic-start index. See [topics.md](topics.md#directory-fields). |
| `0x003e` | 2 | `0x000a` / 10 | `0x00c9` / 201 | Topic count; the entry count for the index at `0x0068`. |
| `0x0040` | 2 | `0x025c` / 604 | `0x025c` / 604 | In-page offset of the stemming table. See [The Stemming Table](#the-stemming-table). |
| `0x0042` | 2 | `0x0000` | `0x0000` | Zero in all 35 fixtures. The stemming table carries its own count in its header rather than here, unlike the `0x003c`/`0x003e` pair. |
| `0x0044` | 8 | EBCDIC `05/24/91` | EBCDIC `08/05/90` | Directory timestamp date, `MM/DD/YY`. The EBCDIC slash bytes always sit at offsets `0x0046` and `0x0049`, which makes the field self-checking. |
| `0x004c` | 2 | `0x01f4` / 500 | `0x01f4` / 500 | Dictionary literal code page. `500` selects CP500, the table that maps compact dictionary literal bytes into 16-bit token words; see [logical-controls.md](logical-controls.md#delta-operation-byte). Constant `500` in all 35 fixtures. |
| `0x004e` | 8 | EBCDIC `07:51:22` | EBCDIC `10:38:16` | Directory timestamp time, `HH:MM:SS`. |
| `0x0056` | 2 | `0x0000` | `0x0000` | Purpose unresolved. Zero in all 35 fixtures. |
| `0x0058` | 2 | `0x0000` | `0x0000` | Purpose unresolved. Zero in all 35 fixtures. |
| `0x005a` | 2 | `0x0000` | `0x0000` | Purpose unresolved. Zero in all 35 fixtures. |

The `QS3X36CM.BOO` timestamp matches the known BookServer URL timestamp for the
same book: `19910524075122` corresponds to `05/24/91 07:51:22`.

### The Directory Page Is A Fixed Region Map

Every offset field above points inside the same 4096-byte directory page, and
the six regions they name follow one another in a fixed order. Reading
`BOO/QS3X36CM.BOO` (directory at file offset `0x1000`), the whole page
accounts for as:

| Directory offset | Named by | Region |
| ---: | --- | --- |
| `0x0000`..`0x0067` | — | Fixed scalars tabulated above. |
| `0x0068` | `0x003c` | Topic-start index. The region is exactly 500 bytes (`0x025c - 0x0068`), which is why one table holds at most `(500 - 4) / 2 = 248` values. |
| `0x025c` | `0x0040` | Stemming table. |
| `0x089c` | `0x001a` | Token-index page table. |
| `0x0c8c` | `0x0022` | One-byte token map: `threshold` big-endian words. `QS3X36CM.BOO` stores `dc00 dc01 dc02 dc08 dc0a ...`, so one-byte token `0` resolves to extended key `0xdc00`. |
| `0x0e44` | `0x0026` | Dictionary token-lookup root index. |
| `0x0e82` | `0x0034` | Content-page logical-record index: `0014 0000 0001 000f 001c ...` — 20 content pages, first record `1`. |

The four indexes at `0x0068`, `0x025c`, `0x089c` and `0x0e82` are all
count-prefixed and all may chain onto a continuation page; the shared chained
layout is documented in [topics.md](topics.md#directory-fields). Directory
offsets `0x005c`..`0x0067` are zero in all 35 fixtures.

### The Two Dictionary Token Counts

The words at `0x002a` and `0x002c` count dictionary tokens, and the difference
between them is exactly the split between the two families of dictionary delta
record described in
[logical-controls.md](logical-controls.md#delta-operation-byte).

- `0x002c` is the **total** number of extended dictionary tokens. Walking every
  `0x0100` dictionary page of a book and counting one token per base record plus
  one per delta record reproduces this word exactly in all 35 fixtures.
- `0x002a` counts the tokens whose record **adds or changes letters** — the base
  record of each dictionary block plus every delta record in mode `1` or mode
  `3`. Counting those alone reproduces `0x002a` exactly in all 35 fixtures.
- `0x002c - 0x002a` is therefore the number of case-only variants, the mode `0`
  and mode `2` delta records.

`QS3X36CM.BOO`: `0x002a` = 2,805 = 312 base + 2,053 mode-1 + 440 mode-3;
`0x002c` = 5,113 = 2,805 + 1,763 mode-0 + 545 mode-2.
`OFCUSEOV.BOO`: `0x002a` = 4,649 = 517 + 3,064 + 1,068; `0x002c` = 6,732 =
4,649 + 1,200 + 883.

The extended token key space is contiguous: the first key is
`threshold * 256`, keys ascend by one per record in dictionary-page order, and
the last key is `threshold * 256 + 0x002c - 1`. Both properties hold in all 35
fixtures.

That also fixes the token threshold. In all 35 fixtures

```text
threshold = min(0xdc, (0xF000 - token_count) / 256)      // integer division
```

so the highest key a book can spell stays below `0xF000` and the one-byte token
space is as large as the extended space allows, capped at `0xdc`. Nine
fixtures sit at the `0xdc` cap; the other 26 are determined by the formula.
A reader should still read the stored threshold — this is a consistency check,
not a substitute.

### The Token-Index Page Table

Directory offset `0x001a` names a small table at directory offset `0x089c`:

```c
struct BooTokenIndexPageTable {
  uint16_t count_be;              // Number of token-index pages.
  struct {
    uint8_t  logical_page_be[3];  // 1-based logical page, as everywhere else.
    uint16_t first_token_key_be;  // First extended token key held by that page.
  } entries[count];
};
```

`QS3X36CM.BOO` stores `0004` followed by `00001b dc00`, `00001c e299`,
`00001d e877`, `00001e eec6` — four pages, logical `27`..`30`, whose first
keys are `0xdc00` (`threshold * 256`), `0xe299` (`determine`), `0xe877`
(`parameter`) and `0xeec6` (`wrkdtadct`).

Verified over all 35 fixtures: the page numbers this table lists are exactly
the pages of the trailing `0x0001` page run, in order; the first key of the
first page is always `threshold * 256`; and the keys ascend. What those pages
hold is described in [pages.md](pages.md#the-trailing-0x0001-run-is-the-token-index).

### The Stemming Table

Directory offset `0x0040` names a table at directory offset `0x025c` that maps
an inflected word's token key to the key of the word it is derived from. It
uses the same chained layout as the other directory indexes, with 4-byte
entries:

```c
struct BooStemmingTable {
  uint16_t count_be;              // Entries in this table.
  uint16_t next_page_be;          // 1-based logical page of the continuation, or 0.
  struct {
    uint16_t inflected_key_be;
    uint16_t stem_key_be;
  } entries[count];
};
```

`QS3X36CM.BOO` stores `013f 0000` — 319 entries, no continuation — and its
first entries resolve to:

| Bytes | Inflected token | Stem token |
| --- | --- | --- |
| `dc5c dc5b` | `abbreviations` | `abbreviation` |
| `dc67 dc66` | `accounting` | `account` |
| `dc70 dc6e` | `adapters` | `adapter` |
| `dc90 dc72` | `added` | `add` |
| `dcea dc72` | `adds` | `add` |
| `dd5b dd5d` | `applies` | `apply` |
| `dd69 ddb5` | `are` | `be` |
| `dd91 dd92` | `authorities` | `authority` |

The stem key is not always lower than the inflected key (`applies`/`apply`,
`authorities`/`authority`), so the table is not an ordering artefact. It is
search-support data: nothing in topic rendering consults it, and a reader that
only renders topics may skip it entirely.

`OFCUSEOV.BOO` stores `018e 0057`: 398 entries in the directory page and a
continuation on logical page `0x57` = 87, whose first word is that
continuation's own count, `0x010b` = 267. See
[pages.md](pages.md#a-continuation-page-has-no-page-header).

### Logical Page Numbering

Directory page-number fields are 1-based logical page numbers in the book
address space, not necessarily absolute physical page indexes in the file. Page
0 stores the physical page that contains logical page 1, which is the directory
page. The mapping is:

```text
physical_page = directory_page_number + logical_page_number - 1
byte_offset = physical_page * 4096
```

Logical page 0 is invalid, and so is any logical page greater than directory
field `0x0016`. This is why `0x0016` equals `file_page_count - 1` only when the
directory page is physical page 1. In shifted books, such as
`SC09-2417-00.boo`, page 0 points to physical page 43 and directory `0x0016` is
147, so the physical last page is `43 + 147 - 1 = 189`, matching the final page
of the 190-page file.

### Version Variants Other Than 2

All 35 fixtures carry EBCDIC `" 1.2"` at directory offset
`0x0010`, so only directory variant 2 is exercised here.

**Unverified hypothesis.** The trailing digit is an EBCDIC decimal digit, so
other directory variants presumably exist and presumably extend the fixed-field
region beyond offset `0x0060`, which is unused in variant-2 fixtures. No
fixture in this repository and no hosted book observed so far uses another
variant, so no other layout is documented here. An independent reader should
reject a directory whose version byte is not EBCDIC `f2` rather than guess a
layout.

## Logical Book Header Controls

The first logical records of a book carry the book-level metadata controls
below. These keys are not stored as raw ASCII or raw EBCDIC strings; see
[logical-controls.md](logical-controls.md) for the record framing, token
reference, and token-resolution details. The metadata envelope begins with a
record that decodes to a leading `L` (`0x004c` as a token word).

| Decoded control key | Value starts after | Meaning |
| --- | ---: | --- |
| `CLANGUAGE=` | 10 bytes | Book language metadata. |
| `CVERSION=` | 9 bytes | Version metadata; superseded by `CBLDVERS=` when both are present. |
| `CBLDVERS=` | 9 bytes | Build/version metadata. |
| `CREFLOW=ON` or `CREFLOW=on` | 10 bytes | Book text is reflowable. |
| `CTITLE=` | 7 bytes | Full title metadata. |
| `CSTITLE=` | 8 bytes | Short title metadata. |
| `CCOPYRIGHT=` | 11 bytes | Copyright metadata. |
| `CSECURITY=` | 10 bytes | Security metadata. Empty in every fixture but `XWEBDEMO.boo`; it does not imply encryption. |
| `CDATE=` | 6 bytes | Date metadata. |
| `CAUTHOR=` | 8 bytes | Author metadata. A book may carry several `CAUTHOR=` controls; `packet.boo` and `XWEBDEMO.boo` each carry one, and multi-author books concatenate them. |
| `CDOCNUM=` | 8 bytes | Document number metadata. It is the last metadata control in the envelope in every fixture, so a decoder can stop there. |

## Working Structures

The following structures are intentionally conservative. They describe how to
locate and parse the verified fields, without assigning meanings that are not
yet known.

```c
struct BooPage0Header {
  uint16_t directory_page_number_be;  // Physical page containing logical page 1.
  uint16_t unknown_0002_be;           // 0x0000 in all 35 fixtures.
  uint32_t object_count_be;           // Resource descriptors; 0 when there are none.
  uint8_t ebcdic_space_padding[4];    // 0x40 bytes.
  uint8_t ebcdic_copyright_text[246];  // 0x000c..0x0101, padded with 0x40.
  uint8_t book_stamp[4];              // 0x0102: binary, or EBCDIC spaces when unset.
  uint8_t ebcdic_space_tail[2];       // 0x0106..0x0107, always 0x40 0x40.
  uint8_t zero_0108[8];               // 0x0000000000000000 in all 35 fixtures.
  uint8_t descriptor_area_header[8];  // 0x0110; see assets.md.
  // Resource-descriptor group 0 starts at 0x0118.
};

struct BooDirectoryPageV2 {
  uint8_t zero_prefix[9];
  uint8_t picture_directory_version[2];  // 0x0009..0x000a; see assets.md.
  uint8_t zero_000b[5];
  uint8_t version_text[4];            // EBCDIC " 1.2" in all 35 fixtures.
  uint8_t token_threshold;            // 0x0014; same value as scalar_0024_be.
  uint8_t ebcdic_zero_digit;          // 0x0015, always 0xf0.
  uint16_t last_page_number_be;       // Last 1-based logical page number.
  uint8_t unknown_0018[2];            // 0x0000 in all 35 fixtures.
  uint16_t token_index_table_offset_be;  // 0x001a; always 0x089c.
  uint8_t unknown_001c[6];            // Zero in all 35 fixtures.
  uint16_t token_map_offset_be;       // 0x0022; directory_base + value.
  uint16_t token_threshold_be;        // 0x0024.
  uint16_t dictionary_root_offset_be; // 0x0026; directory_base + value.
  uint16_t run0100_start_page_be;     // 1-based logical page number.
  uint16_t letter_token_count_be;     // 0x002a.
  uint16_t token_count_be;            // 0x002c.
  uint16_t run0100_page_count_be;
  uint8_t unknown_0030[4];            // Zero in all 35 fixtures.
  uint16_t content_record_index_offset_be;  // 0x0034; directory_base + value.
  uint16_t logical_record_count_be;   // Total logical records in the book.
  uint16_t run0000_page_count_be;
  uint16_t run0000_start_page_be;     // 1-based logical page number.
  uint16_t topic_index_offset_be;     // 0x003c; 0x0068 in all 35 fixtures.
  uint16_t topic_count_be;            // 0x003e.
  uint16_t stemming_table_offset_be;  // 0x0040; 0x025c in all 35 fixtures.
  uint8_t unknown_0042[2];            // 0x0000 in all 35 fixtures.
  uint8_t ebcdic_date[8];             // MM/DD/YY.
  uint16_t dictionary_literal_codepage_be;  // 0x004c; 500 in all 35 fixtures.
  uint8_t ebcdic_time[8];             // HH:MM:SS.
  uint16_t scalar_0056_be;            // Zero in all 35 fixtures.
  uint16_t scalar_0058_be;            // Zero in all 35 fixtures.
  uint16_t scalar_005a_be;            // Zero in all 35 fixtures.
  uint8_t zero_005c[12];              // Zero in all 35 fixtures.
};
```

## Page Runs

The directory fields identify the start/count of the dictionary and content
runs; the trailing `0x0001` run is named by the token-index page table at
`0x089c`. Page roles and the page-class words are documented in
[pages.md](pages.md).

### `QS3X36CM.BOO`

| Page range | First word | Count | Notes |
| --- | ---: | ---: | --- |
| 0 | `0x0001` | 1 | File header; first word points to directory page 1. |
| 1 | `0x0000` | 1 | Directory/header page. |
| 2-6 | `0x0100` | 5 | Matches directory `0x0028=2`, `0x002e=5`. Contains visible EBCDIC terms such as `cipher`, `dspdevd`, `open`, `sndusrmsg`. |
| 7-26 | `0x0000` | 20 | Matches directory `0x003a=7`, `0x0038=20`. Packed token structures, not compressed; see [encoding.md](encoding.md#the-container-applies-no-compression). |
| 27-30 | `0x0001` | 4 | Token index. Matches the four entries of the `0x089c` table. |

### `OFCUSEOV.BOO`

| Page range | First word | Count | Notes |
| --- | ---: | ---: | --- |
| 0 | `0x0001` | 1 | File header; first word points to directory page 1. |
| 1 | `0x0000` | 1 | Directory/header page. |
| 2-9 | `0x0100` | 8 | Matches directory `0x0028=2`, `0x002e=8`. Contains visible EBCDIC terms such as `adding`, `changing`, `embedded`, `procedure`, `workload`. |
| 10-86 | `0x0000` | 77 | Matches directory `0x003a=10`, `0x0038=77`. Packed token structures. |
| 87 | `0x010b` | 1 | Not a page class. This is the stemming table's continuation page; `0x010b` = 267 is that table's own entry count, at page offset `0`. Directory `0x025c` names it: `018e 0057`, continuation on logical page `0x57` = 87. |
| 88-98 | `0x0001` | 11 | Token index. Matches the eleven entries of the `0x089c` table. |

## Open Questions

- The four-byte book stamp at page-0 offset `0x0102`. It is optional (EBCDIC
  spaces in `packet.boo`, `XWEBDEMO.boo` and `HLCRUG21.boo`) and is not derived
  from file size, page count, directory page number, logical-record count,
  topic count, dictionary token count, the directory timestamp, or the
  copyright text — all checked over the 35 fixtures. Two builds of one book, or
  two books differing in one known respect, would settle it.
- Directory scalars `0x0018`, `0x001c`..`0x0021`, `0x0030`..`0x0033`, `0x0042`,
  `0x0056`, `0x0058` and `0x005a` are zero in all 35 fixtures, so the corpus
  cannot say whether they are reserved or simply unused here. A book whose
  build differs — a different code page, a multi-volume shelf member — would
  show which of them are live.
- No repository fixture uses a directory version other than `" 1.2"`, so other
  variants remain undocumented. An implementer should reject any other version
  byte rather than guess a layout.
- What the token-index pages hold per entry. Their framing, ordering and page
  table are documented in [pages.md](pages.md#the-trailing-0x0001-run-is-the-token-index);
  the per-token occurrence payload is not decoded. Nothing in topic rendering
  needs it.
