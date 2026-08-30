# BOO Container And Book Header

This note describes the BOO file header structures that are currently verified
from the repository fixtures and, where noted, from rendered output of the
hosted BookServer service.

Primary fixtures:

- `BOO/QS3X36CM.BOO`, 126,976 bytes, 31 pages of 4096 bytes.
- `BOO/OFCUSEOV.BOO`, 405,504 bytes, 99 pages of 4096 bytes.

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
| `0x000c` | variable | `b4 40 c3 ... f1` | `b4 40 c3 ... f0` | EBCDIC copyright text, padded with `0x40`. |
| `0x0102` | 4 | `df 70 2f 8c` | `3d 85 8f f6` | Unresolved binary value. |

Decoded page-0 text:

| File | Text |
| --- | --- |
| `QS3X36CM.BOO` | `<copyright sign> Copyright IBM Corp. 1991` |
| `OFCUSEOV.BOO` | `<copyright sign> Copyright IBM Corp. 1988, 1990` |

## Directory Page

The directory page starts with a 16-byte prefix, followed by fixed fields and
in-page offsets. In the two primary fixtures this page is physical page 1, so
the absolute file offset is `0x1000 + directory_offset`.

The 16-byte prefix is zero except for the two bytes at directory offsets
`0x0009..0x000a`, which carry the picture-directory layout version. Across the
35 repository fixtures exactly three values occur:

| Bytes at `0x0009..0x000a` | Fixtures | Picture-directory layout |
| --- | ---: | --- |
| `00 00` | 28 | version 1.2, one descriptor group |
| `01 03` | 5 | version 1.3, two descriptor groups |
| `01 00` | 2 | version 1.4, three descriptor groups |

See [assets.md](assets.md) for the descriptor-group layouts these select. This
field is independent of the directory version text below.

Directory offset `0x0010` holds a version string: bytes `40 f1 4b` (EBCDIC
`" 1."`) followed by an EBCDIC digit byte selecting the directory layout
variant. All 35 repository fixtures carry `f2`, EBCDIC `" 1.2"`.

### Fixed Directory Fields

The table below lists the fixed directory offsets whose storage role is
established. Names are descriptive where semantics are known and neutral where
only the storage role is verified. Values without an identified purpose are
marked as such, with the range observed across all 35 repository fixtures.

| Directory offset | Size | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Interpretation |
| ---: | ---: | ---: | ---: | --- |
| `0x0010` | 4 | EBCDIC `" 1.2"` | EBCDIC `" 1.2"` | Directory format/version text. |
| `0x0016` | 2 | `0x001e` / 30 | `0x0062` / 98 | Highest valid 1-based logical page number. For books whose directory is at physical page 1 this equals `file_page_count - 1`; shifted-directory fixtures show it is relative to the directory page. |
| `0x001a` | 2 | `0x089c` | `0x089c` | Purpose unresolved. Constant `0x089c` / 2204 in all 35 fixtures, so it is not derived from book size or content. |
| `0x0022` | 2 | `0x0c8c` | `0x0c8c` | In-page offset of the two-byte token map used for one-byte token IDs. |
| `0x0024` | 2 | `0x00dc` | `0x00d5` | Token threshold; bytes below this value are one-byte token IDs in logical records. |
| `0x0026` | 2 | `0x0e44` | `0x0e38` | In-page offset of the version-2 dictionary token-lookup root index. |
| `0x0028` | 2 | `0x0002` / 2 | `0x0002` / 2 | Start logical page of the observed `0x0100` page run. |
| `0x002c` | 2 | `0x13f9` | `0x1a4c` | Purpose unresolved. Varies per book; no two fixtures share a value. |
| `0x002e` | 2 | `0x0005` / 5 | `0x0008` / 8 | Count of pages in the `0x0100` page run. |
| `0x0034` | 2 | `0x0e82` | `0x0ed2` | In-page offset of the content-page logical-record index; resolve as `directory_base + value`. See [table-of-contents.md](table-of-contents.md). |
| `0x0036` | 2 | `0x00f1` | `0x03f5` | Total logical-record count. Matches the terminal value of the content-page record index in every fixture. |
| `0x0038` | 2 | `0x0014` / 20 | `0x004d` / 77 | Count of pages in the following `0x0000` page run. |
| `0x003a` | 2 | `0x0007` / 7 | `0x000a` / 10 | Start logical page of the following `0x0000` page run. |
| `0x003c` | 2 | `0x0068` / 104 | `0x0068` / 104 | Offset of the first variable table in this directory page. |
| `0x003e` | 2 | `0x000a` / 10 | `0x00c9` / 201 | Entry count for the table at offset `0x0068`. |
| `0x0040` | 2 | `0x025c` / 604 | `0x025c` / 604 | Offset of the second variable table in this directory page. |
| `0x0044` | 8 | EBCDIC `05/24/91` | EBCDIC `08/05/90` | Directory timestamp date, `MM/DD/YY`. The EBCDIC slash bytes always sit at offsets `0x0046` and `0x0049`, which makes the field self-checking. |
| `0x004c` | 2 | `0x01f4` / 500 | `0x01f4` / 500 | Purpose unresolved. Constant `500` in all 35 fixtures. |
| `0x004e` | 8 | EBCDIC `07:51:22` | EBCDIC `10:38:16` | Directory timestamp time, `HH:MM:SS`. |
| `0x0056` | 2 | `0x0000` | `0x0000` | Purpose unresolved. Zero in all 35 fixtures. |
| `0x0058` | 2 | `0x0000` | `0x0000` | Purpose unresolved. Zero in all 35 fixtures. |
| `0x005a` | 2 | `0x0000` | `0x0000` | Purpose unresolved. Zero in all 35 fixtures. |
| `0x003c` table | variable | starts `00 0a 00 02 ...` | starts `00 c9 00 03 ...` | First variable table; first word repeats the count. |
| `0x0040` table | variable | starts `01 3f 00 00 ...` | starts `01 8e 00 57 ...` | Second variable table; entry layout unresolved. |

The `QS3X36CM.BOO` timestamp matches the known BookServer URL timestamp for the
same book: `19910524075122` corresponds to `05/24/91 07:51:22`.

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

All 35 `.BOO` files in this repository carry EBCDIC `" 1.2"` at directory offset
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
  uint16_t unknown_0002_be;           // 0x0000 in both fixtures.
  uint32_t unknown_0004_be;           // 0x00000000 in both fixtures.
  uint8_t ebcdic_space_padding[4];    // 0x40 bytes.
  uint8_t ebcdic_copyright_text[];    // Starts at 0x000c, padded with 0x40.
  // Unresolved 4-byte binary value at absolute file offset 0x0102.
};

struct BooDirectoryPageV2 {
  uint8_t zero_prefix[0x10];
  uint8_t version_text[4];            // EBCDIC " 1.2" in both fixtures.
  uint8_t unknown_0014[2];
  uint16_t last_page_number_be;       // Last 1-based logical page number.
  uint8_t unknown_0018[2];
  uint16_t scalar_001a_be;
  uint8_t unknown_001c[6];
  uint16_t ptr_0022_be;               // directory_base + value.
  uint16_t scalar_0024_be;
  uint16_t scalar_0026_be;
  uint16_t run0100_start_page_be;     // 1-based logical page number.
  uint8_t unknown_002a[2];
  uint16_t scalar_002c_be;
  uint16_t run0100_page_count_be;
  uint8_t unknown_0030[4];
  uint16_t ptr_0034_be;               // directory_base + value.
  uint16_t logical_record_count_be;   // Total logical records in the book.
  uint16_t run0000_page_count_be;
  uint16_t run0000_start_page_be;     // 1-based logical page number.
  uint16_t table1_offset_be;          // 0x0068 in both fixtures.
  uint16_t table1_count_be;
  uint16_t table2_offset_be;          // 0x025c in both fixtures.
  uint8_t unknown_0042[2];
  uint8_t ebcdic_date[8];             // MM/DD/YY.
  uint16_t selected_v2_value_be;      // 0x01f4 in both fixtures.
  uint8_t ebcdic_time[8];             // HH:MM:SS.
  uint16_t scalar_0056_be;
  uint16_t scalar_0058_be;
  uint16_t scalar_005a_be;
};
```

## Page Runs

The directory fields identify the start/count of the first two content runs.
The meaning of the page-class words themselves is still unresolved.

### `QS3X36CM.BOO`

| Page range | First word | Count | Notes |
| --- | ---: | ---: | --- |
| 0 | `0x0001` | 1 | File header; first word points to directory page 1. |
| 1 | `0x0000` | 1 | Directory/header page. |
| 2-6 | `0x0100` | 5 | Matches directory `0x0028=2`, `0x002e=5`. Contains visible EBCDIC terms such as `cipher`, `dspdevd`, `open`, `sndusrmsg`. |
| 7-26 | `0x0000` | 20 | Matches directory `0x003a=7`, `0x0038=20`. Mostly binary/compressed-looking data. |
| 27-30 | `0x0001` | 4 | Trailing run, likely index/resource/control pages. |

### `OFCUSEOV.BOO`

| Page range | First word | Count | Notes |
| --- | ---: | ---: | --- |
| 0 | `0x0001` | 1 | File header; first word points to directory page 1. |
| 1 | `0x0000` | 1 | Directory/header page. |
| 2-9 | `0x0100` | 8 | Matches directory `0x0028=2`, `0x002e=8`. Contains visible EBCDIC terms such as `adding`, `changing`, `embedded`, `procedure`, `workload`. |
| 10-86 | `0x0000` | 77 | Matches directory `0x003a=10`, `0x0038=77`. Mostly binary/compressed-looking data. |
| 87 | `0x010b` | 1 | Isolated page class not yet understood. |
| 88-98 | `0x0001` | 11 | Trailing run, likely index/resource/control pages. |

## Open Questions

- Page-0 offset `0x0102` changes between samples and is not yet explained.
- Several fixed directory scalars have a stable storage position but no
  identified meaning: `0x001a` (constant `0x089c`), `0x002c` (per-book), and
  `0x004c` (constant `500`).
- The second directory variable table at offset `0x025c` needs entry-level
  decoding.
- No repository fixture uses a directory version other than `" 1.2"`, so other
  variants remain undocumented.
- The full dictionary delta/update grammar still needs to be mapped so that
  every tokenized logical control decodes from the documented rules alone.
