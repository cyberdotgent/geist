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
- The dictionary literal codepage table selected by directory word `0x004c`
  maps compact dictionary bytes to 16-bit token words. In the repository
  fixtures this table is the reader's CP500 table. It is not the final display
  decoder.
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
- Dictionary delta records can remap the current reconstructed token-word
  buffer before appending or changing case. For the bundled code-page-500
  samples, the normal/lowercase-oriented buffer mapping uses table numbers
  `(word >> 11) + 19`, and the uppercase-oriented buffer mapping uses
  `(word >> 11) + 13`. These transforms happen before final text output.
- For DBCS/stateful code pages (`933`, `935`, `937`, `939`), the reader emits
  `0x0e` and `0x0f` shift bytes around double-byte runs. Several multibyte code
  pages can emit full 16-bit table values.
- Dictionary lookup kind `2` uses a separate reverse search-key encoding path
  (`BooEncodeUnicodeWordsToSearchBytes` and
  `BooEncodeUnicodeWordToSearchByte`) rather than the output translation-table
  decoder.

See [boo-header.md](boo-header.md) for the current header and page-run
evidence.

## Failure Mode To Avoid

Do not decode dictionary literal bytes with the CP500 table and then treat the
resulting low-valued token words as final ASCII or Unicode text. That loses the
reader's table-backed normal/uppercase/lowercase transforms and bypasses the
final BOO translation-table pages. It also does not account for the logical
record iterator's token-boundary spacing. The visible symptom is metadata such
as `as/400CommandCross-reference` or `CopyrightibmCorp.1991` instead of the
BookServer output `AS/400 Command Cross-Reference` and
`Copyright IBM Corp. 1991`.

## Open Questions

- Complete byte-for-byte behavior for every supported code page and fallback
  substitution path.
- Fixture evidence for the dictionary index controls that the reader supports
  but the repository fixtures did not need on the sampled root-to-terminal
  paths (`2`, `4`, and `5`).
- Whether body text uses the same logical-record tokenization path throughout
  all page classes, or whether some page classes have additional text storage
  variants.
- Complete field-level documentation of the logical-record iterator's spacing
  and suppression controls.
