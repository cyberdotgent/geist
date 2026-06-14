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

## Display Case Recovery

BookServer output verifies that topic body text is rendered with normal sentence
case, not title case. For example, `QS3X36CM.BOO` topic `1.0` renders as
`This manual is designed as a cross-reference...` and preserves product/key
spellings such as `System/36`, `AS/400`, and `(CL)`.

The connected `ephwam.dll` IDB verifies that this casing is not a Markdown or
HTML renderer convention. It is part of dictionary-token reconstruction in
`BooApplyDictionaryDeltaRecord`:

| Delta mode | Reader behavior | libgeist representation note |
| --- | --- | --- |
| `0` | Run `BooMapTokenWordBufferUpperTable` over the current word buffer, then run `BooMapTokenWordToLower` for each listed byte position plus one. | `libgeist` stores buffers without the reader's leading length word, so the listed byte is used as a zero-based word index. |
| `1` | Set the retained prefix length, read a following literal-count byte, run `BooMapTokenWordBufferNormalTable` over the retained buffer, then append literal words. | The literal payload is copied after the normal-table transform. |
| `2` | Run `BooMapTokenWordBufferNormalTable` over the current word buffer, then run `BooMapTokenWordToUpper` for each listed byte position plus one. | This is the common sentence-case path: normalize the token to lower/normal case, then uppercase selected characters. |
| `3` | Run `BooMapTokenWordBufferNormalTable` over the current word buffer, then append literal words. | Same append path as mode `1`, without changing the retained length first. |

The reader's mapping functions use code-page-specific translation-table pages:
`BooMapTokenWordBufferNormalTable`, `BooMapTokenWordBufferUpperTable`,
`BooMapTokenWordToUpper`, and `BooMapTokenWordToLower` all call
`BooLoadTranslationTablePage`. The current `libgeist` implementation has an
ASCII-only approximation of those table mappings; non-ASCII and some
code-page-specific case behavior still requires full translation-table loading.

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
