# BOO Encoding and Tokenization

This page summarizes character encoding, tokenization, and translation-table
mechanisms in BOO files. The complete logical-control decoding path is in
[logical-controls.md](logical-controls.md).

## Verified

- Text observed in the page-0 and page-1 headers decodes as EBCDIC code page
  037.
- Logical book header controls are stored as tokenized logical records, not as
  raw ASCII or EBCDIC strings. See
  [logical-controls.md](logical-controls.md).
- Logical-record payloads contain token references. Bytes below the directory
  token threshold are one-byte token IDs; bytes at or above the threshold begin
  two-byte extended token references in the version-2 fixtures.
- Resolved token text is a word-counted sequence of 16-bit token words.
- A token word uses the upper five bits for the translation table selector and
  the lower eleven bits for the entry index:

```c
table_no = (word >> 11) + 1;
table_index = word & 0x07ff;
```

- Translation tables are ordinary 4096-byte BOO pages loaded by table number
  relative to the directory page:

```c
physical_page = directory_page_number + table_no - 1;
file_offset = physical_page * 4096;
```

- Each translation-table entry is a big-endian 16-bit value at
  `table_page + table_index * 2`.
- In the common single-byte path, the reader emits the low byte of the table
  value. Values above `0x2fff` in the original token word, and decoded byte
  `0x1a`, are replaced with the reader substitution byte.
- For DBCS/stateful code pages (`933`, `935`, `937`, `939`), the reader emits
  `0x0e` and `0x0f` shift bytes around double-byte runs. Several multibyte code
  pages can emit full 16-bit table values.
- Dictionary lookup kind `2` uses a separate reverse search-key encoding path
  (`BooEncodeUnicodeWordsToSearchBytes` and
  `BooEncodeUnicodeWordToSearchByte`) rather than the output translation-table
  decoder.

See [boo-header.md](boo-header.md) for the current header and page-run
evidence.

## Open Questions

- Complete byte-for-byte behavior for every supported code page and fallback
  substitution path.
- Full subfield layout of dictionary index-entry continuation payloads after
  the token key.
- Whether body text uses the same logical-record tokenization path throughout
  all page classes, or whether some page classes have additional text storage
  variants.
