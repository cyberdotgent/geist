# BOO Compression

This page records the current compression analysis for BOO files.

## Verified

- No standard compression algorithm has been identified in the connected
  BookManager parsing library (`ephwam.dll`, SHA-256
  `bac3d7e216dd45b0d1307747f43170050da9db961967dd409f965bd9917d0406`).
- The reader's raw page path loads fixed 4096-byte BOO pages directly:
  `BooReadPhysicalPageIntoBuffer` seeks to
  `((directory_page + logical_page_number) << 12) - 4096` and reads exactly
  4096 bytes with the CRT `fread` wrapper. No decompression, decryption, or
  transform is applied in this path.
- `BooGetOrLoadPageBuffer` is a cache helper around the same raw page load.
  It returns an existing cached page buffer or allocates a new buffer and calls
  the raw page reader.
- Searches for standard compression indicators in the IDB found no zlib/gzip
  magic constants, no CRC/adler constants, no inflate/deflate/LZW/Huffman
  strings, and no compression library imports.
- The binary-looking `0x0000` content runs are currently best described as
  packed/tokenized BookManager structures rather than a general-purpose
  compressed byte stream. Logical text decoding happens through compact record
  lengths, token references, dictionary delta records, token-word translation
  tables, and logical-record spacing rules documented in
  [logical-controls.md](logical-controls.md) and [encoding.md](encoding.md).

See [boo-header.md](boo-header.md) for the page-run evidence.

## Reader-Code Evidence

The connected `ephwam.dll` IDB has the following high-confidence names:

| IDA name | Address | Evidence |
| --- | ---: | --- |
| `BooReadPhysicalPageIntoBuffer` | `0x12106ea` | Opens the BOO file if needed, seeks to the calculated physical page offset, and reads one 4096-byte page. |
| `BooGetOrLoadPageBuffer` | `0x1210a1a` | Checks the page cache, allocates a buffer when needed, and loads a raw page through `BooReadPhysicalPageIntoBuffer`. |
| `BooReadBE16` | `0x1231fce` | Big-endian 16-bit integer helper used throughout page/directory parsing. |

The exported function named `Scm_Getcsum` is now identified in IDA as
`BooGetCurrentSummaryText` (`0x121f351`). Despite the historical export name,
it walks logical records and copies summary text; it does not compute a
checksum.

## Open Questions

- Whether different page classes use different compression schemes.
- Whether any page class outside the paths sampled so far uses a separate
  packed representation not exercised by the bundled fixtures.
- Complete independent documentation of all body-text and index tokenization
  variants.
