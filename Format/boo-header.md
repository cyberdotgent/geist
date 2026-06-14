# BOO Header Observations

This note records the currently verified BOO container header observations from
the repository fixtures:

- `BOO/QS3X36CM.BOO`, 126,976 bytes, 31 pages of 4096 bytes.
- `BOO/OFCUSEOV.BOO`, 405,504 bytes, 99 pages of 4096 bytes.

All multi-byte binary fields below are interpreted as big-endian unless noted.
Text bytes observed in the header decode as EBCDIC code page 037.

## File Granularity

Both samples are exact multiples of 4096 bytes, and meaningful structures start
on 4096-byte boundaries.

| File | Size | 4096-byte pages |
| --- | ---: | ---: |
| `QS3X36CM.BOO` | `0x0001f000` / 126,976 | 31 |
| `OFCUSEOV.BOO` | `0x00063000` / 405,504 | 99 |

## Page 0: Fixed File Header

Page 0 begins with a small binary prefix followed by EBCDIC copyright text. The
rest of the first 4096-byte page is padded with EBCDIC spaces (`0x40`), except
for one four-byte value at offset `0x0102`.

| Offset | Size | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Interpretation |
| ---: | ---: | --- | --- | --- |
| `0x0000` | 4 | `00 01 00 00` | `00 01 00 00` | Likely BOO/container version or magic-like signature. |
| `0x0004` | 4 | `00 00 00 00` | `00 00 00 00` | Unknown zero field. |
| `0x0008` | 4 | `40 40 40 40` | `40 40 40 40` | EBCDIC spaces. |
| `0x000c` | variable | `b4 40 c3 ... f1` | `b4 40 c3 ... f0` | EBCDIC copyright string. `0xb4` decodes as copyright sign in CP037. |
| `0x0102` | 4 | `df 70 2f 8c` | `3d 85 8f f6` | Unknown binary value; possibly checksum or identifier. |

Decoded copyright strings:

| File | Page-0 text |
| --- | --- |
| `QS3X36CM.BOO` | `<copyright sign> Copyright IBM Corp. 1991` |
| `OFCUSEOV.BOO` | `<copyright sign> Copyright IBM Corp. 1988, 1990` |

## Page 1: Directory/Header Page

Page 1 is a binary directory/header page. It starts with 16 zero bytes, then a
mixture of short binary fields, EBCDIC text, and offset tables. The fields below
are the ones that currently correlate directly with the observed page layout.

| Page-1 offset | Size | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Interpretation |
| ---: | ---: | ---: | ---: | --- |
| `0x0010` | 4 | EBCDIC ` 1.2` | EBCDIC ` 1.2` | Header or format version text. |
| `0x0016` | 2 | `0x001e` / 30 | `0x0062` / 98 | Last page number, equal to `page_count - 1`. |
| `0x0028` | 2 | `0x0002` / 2 | `0x0002` / 2 | Start page of the `0x0100` page run. |
| `0x002e` | 2 | `0x0005` / 5 | `0x0008` / 8 | Count of pages in the `0x0100` page run. |
| `0x0038` | 2 | `0x0014` / 20 | `0x004d` / 77 | Count of pages in the following `0x0000` page run. |
| `0x003a` | 2 | `0x0007` / 7 | `0x000a` / 10 | Start page of the following `0x0000` page run. |
| `0x003c` | 2 | `0x0068` / 104 | `0x0068` / 104 | Offset in page 1 of a variable-length table header. |
| `0x003e` | 2 | `0x000a` / 10 | `0x00c9` / 201 | Entry count for the table at page-1 offset `0x0068`. |
| `0x0040` | 2 | `0x025c` / 604 | `0x025c` / 604 | Offset in page 1 of a second table. |
| `0x0044` | 8 | EBCDIC `05/24/91` | EBCDIC `08/05/90` | Date field. |
| `0x004c` | 2 | `0x01f4` / 500 | `0x01f4` / 500 | Unknown constant; possibly code page or format parameter. |
| `0x004e` | 8 | EBCDIC `07:51:22` | EBCDIC `10:38:16` | Time field. |
| `0x0068` | 2 | `0x000a` / 10 | `0x00c9` / 201 | Repeated table entry count. |
| `0x006c` | variable | `00 02 00 03 ...` | `00 03 00 04 ...` | Big-endian 16-bit table entries. Meaning unresolved. |
| `0x025c` | variable | begins `01 3f ...` | begins `01 8e ...` | Second table. Meaning unresolved. |

The timestamp in `QS3X36CM.BOO` matches the BookServer URL timestamp known for
the same book: `19910524075122` corresponds to `05/24/91 07:51:22`.

## Page Runs

The first two bytes of each 4096-byte page form a page class or flags field. The
page-1 directory fields above identify the start/count of the first two content
runs exactly.

### `QS3X36CM.BOO`

| Page range | First word | Count | Notes |
| --- | ---: | ---: | --- |
| 0 | `0x0001` | 1 | Fixed file header. |
| 1 | `0x0000` | 1 | Directory/header page. |
| 2-6 | `0x0100` | 5 | Matches page-1 `0x0028=2`, `0x002e=5`. Contains visible EBCDIC terms such as `cipher`, `dspdevd`, `open`, `sndusrmsg`. |
| 7-26 | `0x0000` | 20 | Matches page-1 `0x003a=7`, `0x0038=20`. Mostly binary/compressed-looking data. |
| 27-30 | `0x0001` | 4 | Trailing run, likely index/resource/control pages. |

### `OFCUSEOV.BOO`

| Page range | First word | Count | Notes |
| --- | ---: | ---: | --- |
| 0 | `0x0001` | 1 | Fixed file header. |
| 1 | `0x0000` | 1 | Directory/header page. |
| 2-9 | `0x0100` | 8 | Matches page-1 `0x0028=2`, `0x002e=8`. Contains visible EBCDIC terms such as `adding`, `changing`, `embedded`, `procedure`, `workload`. |
| 10-86 | `0x0000` | 77 | Matches page-1 `0x003a=10`, `0x0038=77`. Mostly binary/compressed-looking data. |
| 87 | `0x010b` | 1 | Isolated page class not yet understood. |
| 88-98 | `0x0001` | 11 | Trailing run, likely index/resource/control pages. |

## Working Header Structure

The following provisional structure is supported by the current samples:

```c
struct BooPage0Header {
  uint32_t signature_or_version;   // 0x00010000 in both samples
  uint32_t zero0;
  char ebcdic_space_padding[4];
  char ebcdic_copyright_text[];    // starts at 0x000c, padded with 0x40
  // unknown 4-byte binary field at absolute file offset 0x0102
};

struct BooPage1Directory {
  uint8_t zero_prefix[0x10];
  char version_text[4];            // EBCDIC " 1.2"
  uint8_t unknown_0x14[2];
  uint16_t last_page_number;       // page_count - 1
  uint8_t unknown_0x18_to_0x27[0x10];
  uint16_t run0100_start_page;
  uint8_t unknown_0x2a_to_0x2d[4];
  uint16_t run0100_page_count;
  uint8_t unknown_0x30_to_0x37[8];
  uint16_t run0000_page_count;
  uint16_t run0000_start_page;
  uint16_t table1_offset;          // 0x0068 in both samples
  uint16_t table1_count;
  uint16_t table2_offset;          // 0x025c in both samples
  uint8_t unknown_0x42[2];
  char ebcdic_date[8];             // MM/DD/YY
  uint16_t unknown_constant_01f4;
  char ebcdic_time[8];             // HH:MM:SS
};
```

## Open Questions

- The meaning of page-0 offset `0x0102` is unknown. It changes between samples
  and does not correlate with file size or page count.
- The byte at page-1 offset `0x0014` differs (`0xdc` versus `0xd5`) and is not
  yet explained. It may be part of a longer version/build identifier rather than
  text.
- Page-1 offsets `0x001a`, `0x0022`, `0x0024`, `0x0026`, `0x002a`, `0x002c`,
  `0x0034`, and `0x0036` are non-zero but not yet interpreted.
- The table at page-1 offset `0x0068` and the second table at `0x025c` likely
  index later content, but their entry semantics are unresolved.
- The relationship between page classes `0x0100`, `0x0000`, `0x0001`, and the
  isolated `0x010b` page in `OFCUSEOV.BOO` still needs reader-code or broader
  fixture evidence.
