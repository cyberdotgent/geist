# BOO Container And Book Header

This note describes the BOO file header structures that are currently verified
from the repository fixtures and from the IBM `ephwam.dll` reader code.

Fixtures:

- `BOO/QS3X36CM.BOO`, 126,976 bytes, 31 pages of 4096 bytes.
- `BOO/OFCUSEOV.BOO`, 405,504 bytes, 99 pages of 4096 bytes.

Reader-code evidence:

- `ephwam.dll` `Scm_Bopen` calls an internal open routine, which calls the
  directory-page parser and then parses logical book header controls.
- The directory-page parser uses 4096-byte addressing by left-shifting page
  numbers by 12.
- The two-byte helper at `ephwam.dll` `0x1231fce` returns
  `bytes[1] + (bytes[0] << 8)`, so two-byte file fields listed here are
  big-endian.
- EBCDIC text observed in the physical header bytes decodes as code page 037.

Relevant reader routines:

| Routine | Address | Format evidence |
| --- | ---: | --- |
| `Scm_Bopen` | `0x12181a1` | Public book-open entry point. |
| Internal book-open wrapper | `0x1218175` | Calls the physical directory parser, then logical header-control parser. |
| Physical directory setup/parser | `0x1217d8d` | Reads page-0 directory pointer, seeks to `page << 12`, validates file extent, and loads page buffers. |
| Directory field parser | `0x1216de9` | Validates directory version bytes and consumes fixed directory offsets. |
| Logical header-control parser | `0x1217645` | Decodes records and recognizes `CLANGUAGE=`, `CTITLE=`, `CDOCNUM=`, and related controls. |

## Physical Layout

Both sample files are exact multiples of 4096 bytes. A BOO reader should treat
the file as a sequence of 4096-byte physical pages.

| File | Size | Physical pages |
| --- | ---: | ---: |
| `QS3X36CM.BOO` | `0x0001f000` / 126,976 | 31 |
| `OFCUSEOV.BOO` | `0x00063000` / 405,504 | 99 |

## Page 0: File Header And Directory Locator

Page 0 is not the main directory. The first two bytes are a big-endian physical
page number for the directory/header page. The reader reads this word, shifts it
left by 12, seeks to that byte offset, and parses the directory there. In both
repository fixtures the directory page number is `0x0001`, so the directory
begins at file offset `0x1000`.

| Absolute offset | Size | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Meaning |
| ---: | ---: | --- | --- | --- |
| `0x0000` | 2 | `00 01` | `00 01` | Big-endian directory page number. |
| `0x0002` | 2 | `00 00` | `00 00` | Unresolved; not consumed by the observed open path. |
| `0x0004` | 4 | `00 00 00 00` | `00 00 00 00` | Unresolved zero field. |
| `0x0008` | 4 | `40 40 40 40` | `40 40 40 40` | EBCDIC spaces. |
| `0x000c` | variable | `b4 40 c3 ... f1` | `b4 40 c3 ... f0` | EBCDIC copyright text, padded with `0x40`. |
| `0x0102` | 4 | `df 70 2f 8c` | `3d 85 8f f6` | Unresolved binary value. |

Decoded page-0 text:

| File | Text |
| --- | --- |
| `QS3X36CM.BOO` | `<copyright sign> Copyright IBM Corp. 1991` |
| `OFCUSEOV.BOO` | `<copyright sign> Copyright IBM Corp. 1988, 1990` |

## Directory Page

The directory page starts with 16 zero bytes, followed by fixed fields and
in-page offsets. In the two fixtures this page is physical page 1, so the
absolute file offset is `0x1000 + directory_offset`.

The reader validates the version string at directory offset `0x0010`. It checks
for bytes `40 f1 4b` followed by one of the accepted EBCDIC digit bytes:

- `f0`: version variant 0, treated as older layout.
- `f1`: version variant 1.
- `f2`: version variant 2, used by both repository fixtures.
- `f3`: version variant 3, extended layout with additional fields.

In both fixtures the bytes at `0x0010..0x0013` are EBCDIC `" 1.2"`.

### Fixed Fields Used By The Reader

The table below lists fixed directory offsets that are read by the observed
reader open path. Names are descriptive where semantics are known and neutral
where only the storage role is verified.

| Directory offset | Size | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Verified parser behavior |
| ---: | ---: | ---: | ---: | --- |
| `0x0010` | 4 | EBCDIC `" 1.2"` | EBCDIC `" 1.2"` | Format/version text checked by the reader. |
| `0x0016` | 2 | `0x001e` / 30 | `0x0062` / 98 | Last physical page number; equals `page_count - 1`. |
| `0x001a` | 2 | `0x089c` | `0x089c` | Scalar read by the parser; semantic unresolved. |
| `0x0022` | 2 | `0x0c8c` | `0x0c8c` | In-page offset. Reader resolves it as `directory_base + value`. |
| `0x0024` | 2 | `0x00dc` | `0x00d5` | Scalar read by the parser; semantic unresolved. |
| `0x0026` | 2 | `0x0e44` | `0x0e38` | Scalar read by the parser; semantic unresolved. |
| `0x0028` | 2 | `0x0002` / 2 | `0x0002` / 2 | Start page of the observed `0x0100` page run. |
| `0x002c` | 2 | `0x13f9` | `0x1a4c` | Used with directory byte `0x0014` in an internal derived value. |
| `0x002e` | 2 | `0x0005` / 5 | `0x0008` / 8 | Count of pages in the `0x0100` page run. |
| `0x0034` | 2 | `0x0e82` | `0x0ed2` | In-page offset. Reader resolves it as `directory_base + value`. |
| `0x0036` | 2 | `0x00f1` | `0x03f5` | Scalar read by the parser; semantic unresolved. |
| `0x0038` | 2 | `0x0014` / 20 | `0x004d` / 77 | Count of pages in the following `0x0000` page run. |
| `0x003a` | 2 | `0x0007` / 7 | `0x000a` / 10 | Start page of the following `0x0000` page run. |
| `0x003c` | 2 | `0x0068` / 104 | `0x0068` / 104 | Offset of the first variable table in this directory page. |
| `0x003e` | 2 | `0x000a` / 10 | `0x00c9` / 201 | Entry count for the table at offset `0x0068`. |
| `0x0040` | 2 | `0x025c` / 604 | `0x025c` / 604 | Offset of the second variable table in this directory page. |
| `0x0044` | 8 | EBCDIC `05/24/91` | EBCDIC `08/05/90` | Directory timestamp date, `MM/DD/YY`; the reader validates the EBCDIC slash bytes at offsets `0x0046` and `0x0049`. |
| `0x004c` | 2 | `0x01f4` / 500 | `0x01f4` / 500 | Value selected by the version-2 parser branch. |
| `0x004e` | 8 | EBCDIC `07:51:22` | EBCDIC `10:38:16` | Directory timestamp time, `HH:MM:SS`. |
| `0x0056` | 2 | `0x0000` | `0x0000` | Scalar read by the parser; semantic unresolved. |
| `0x0058` | 2 | `0x0000` | `0x0000` | Scalar read by the parser; semantic unresolved. |
| `0x005a` | 2 | `0x0000` | `0x0000` | Scalar read by the parser; semantic unresolved. |
| `0x003c` table | variable | starts `00 0a 00 02 ...` | starts `00 c9 00 03 ...` | First variable table; first word repeats the count. |
| `0x0040` table | variable | starts `01 3f 00 00 ...` | starts `01 8e 00 57 ...` | Second variable table; entry layout unresolved. |

The `QS3X36CM.BOO` timestamp matches the known BookServer URL timestamp for the
same book: `19910524075122` corresponds to `05/24/91 07:51:22`.

### Version-3 Extended Fields

The observed fixtures are version variant 2, so these fields are not verified
with repository samples. The reader has a branch for version variant 3 and reads
additional fixed offsets:

| Directory offset | Size | Parser behavior in version-3 branch |
| ---: | ---: | --- |
| `0x0008` | 4 | Big-endian 32-bit value. |
| `0x0032` | 2 | Scalar value. |
| `0x007a` | 2 | Big-endian value combined into a 32-bit internal field. |
| `0x007c` | 2 | Big-endian value combined into a 32-bit internal field. |
| `0x007e` | 2 | High half of a big-endian 32-bit value. |
| `0x0080` | 2 | Low half of a big-endian 32-bit value. |
| `0x0082` | 2 | High half of a big-endian 32-bit value. |
| `0x0084` | 2 | Low half of a big-endian 32-bit value. |
| `0x008a` | 2 | Big-endian scalar. |
| `0x008c` | 4 | Big-endian 32-bit value. |
| `0x0090` | 4 | Big-endian 32-bit value. |
| `0x0094` | 4 | Big-endian 32-bit value. |
| `0x0098` | 4 | Big-endian 32-bit value. |
| `0x009c` | 4 | Big-endian 32-bit value. |
| `0x00a0` | 4 | Big-endian 32-bit value. |
| `0x00a4` | 4 | Big-endian 32-bit value. |
| `0x00a8` | 4 | Big-endian 32-bit value. |
| `0x00ac` | 4 | Big-endian 32-bit value. |
| `0x00b0` | 4 | Big-endian 32-bit value. |

Because neither repository fixture uses this branch, an independent reader
should parse these only after confirming the version byte is EBCDIC `f3` and
should treat the meanings as unresolved.

## Logical Book Header Controls

After the physical directory is parsed, the reader iterates tokenized logical
records, decodes each record to text, and recognizes the following control keys.
These keys are not stored as raw ASCII or raw EBCDIC strings; see
[logical-controls.md](logical-controls.md) for the record framing, token
reference, and token-resolution details.

| Decoded control key | Value starts after | Verified reader use |
| --- | ---: | --- |
| `CLANGUAGE=` | 10 bytes | Book language metadata. |
| `CVERSION=` | 9 bytes | Version metadata if no build-version override has been seen. |
| `CBLDVERS=` | 9 bytes | Build/version metadata; overrides `CVERSION=`. |
| `CREFLOW=ON` or `CREFLOW=on` | 10 bytes | Enables a reflow flag. |
| `CTITLE=` | 7 bytes | Full title metadata. |
| `CSTITLE=` | 8 bytes | Short title metadata. |
| `CCOPYRIGHT=` | 11 bytes | Copyright metadata. |
| `CSECURITY=` | 10 bytes | Security metadata. |
| `CDATE=` | 6 bytes | Date metadata. |
| `CAUTHOR=` | 8 bytes | Author metadata; repeated author controls are concatenated with two spaces while under the reader's size limit. |
| `CDOCNUM=` | 8 bytes | Document number metadata; terminates the metadata scan in the observed parser path. |

The logical-control parser expects the first selected record to decode to a
record beginning with `0x004c` (`'L'`). The records are converted to
NUL-terminated strings in reader memory before the key comparisons above.

## Working Structures

The following structures are intentionally conservative. They describe how to
locate and parse the verified fields, without assigning meanings that are not
yet known.

```c
struct BooPage0Header {
  uint16_t directory_page_number_be;  // 0x0001 in both fixtures.
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
  uint16_t last_page_number_be;       // page_count - 1.
  uint8_t unknown_0018[2];
  uint16_t scalar_001a_be;
  uint8_t unknown_001c[6];
  uint16_t ptr_0022_be;               // directory_base + value.
  uint16_t scalar_0024_be;
  uint16_t scalar_0026_be;
  uint16_t run0100_start_page_be;
  uint8_t unknown_002a[2];
  uint16_t scalar_002c_be;
  uint16_t run0100_page_count_be;
  uint8_t unknown_0030[4];
  uint16_t ptr_0034_be;               // directory_base + value.
  uint16_t scalar_0036_be;
  uint16_t run0000_page_count_be;
  uint16_t run0000_start_page_be;
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
- The semantics of many fixed directory scalars are unresolved even though their
  offsets are now confirmed as reader-consumed fields.
- The first and second directory variable tables at offsets `0x0068` and
  `0x025c` need entry-level decoding.
- Version variant 3 fields are known from reader code but not verified with a
  repository fixture.
- The full dictionary delta/update grammar needs to be mapped so all tokenized
  logical controls can be decoded without relying on the IBM reader.
