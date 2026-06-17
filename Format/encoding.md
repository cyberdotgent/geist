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

## Logical Record Spacing Controls

Resolved logical-record tokens are not simply concatenated. The first token word
can be a low-valued spacing prefix. When present, the prefix is consumed as
layout metadata and is not emitted as text.

The visible-text assembler should maintain the distinction between real text and
synthetic inter-token spacing:

1. Resolve each token reference to a token-word sequence.
2. If the first token word is below `4`, remove it from the visible sequence and
   treat it as the spacing prefix for this token.
3. Prefix `0` suppresses the pending inter-token space before this token. It
   must remove only a synthetic space that was inserted by the assembler, not a
   real visible character.
4. Prefix `1` also suppresses the pending inter-token space before this token,
   then emits the remaining token words. This is observed before punctuation
   such as the period after generated topic numbers.
5. Prefix `2` means no synthetic space should be appended after this token.
6. Other prefixes or tokens without a low-valued prefix use the default
   inter-token spacing behavior observed in version-2 fixtures: append a
   synthetic space after the token unless the token already ends in a space.

The important implementation rule is that spacing prefixes operate on the
assembler's pending blank, not on arbitrary output bytes. Consecutive prefix-0
tokens are therefore idempotent when no synthetic space remains.

Evidence from `BOO/packet.boo` topic `COVER`:

| Payload offset | Token bytes | Resolved token text | Interpretation |
| --- | --- | --- | --- |
| `0x411c6` | `dd e1` | `File` | Visible label word. |
| `0x411c8` | `e4 26` | `Number` | Visible label word. |
| `0x411ca` | `8c` | `PACKET` | Full file-number metadata value. |
| `0x411cb` | `00` | spacing prefix `0` | Removes the synthetic space after `PACKET`. |
| `0x411cc` | `00` | spacing prefix `0` | No-op because the synthetic space was already removed. |
| `0x411cd` | `06` | padding/control-like separator run | Not visible text. |

BookServer renders the same topic as `File Number PACKET`. If an
implementation lets both consecutive `0` prefixes pop output bytes, the second
one deletes the visible `T` and incorrectly renders `PACKE`.

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

## Character Classes And Special Characters

The reader-code path documented in `logical-controls.md` decodes logical-record
text in two stages: dictionary literal bytes first become 16-bit token words
through the selected code-page table, then the final text decoder emits display
bytes/characters from those token words. The bundled packet fixture uses the
CP500 dictionary literal table. That table includes ordinary ASCII punctuation
and Latin-1 symbols; these are not BookMaster entity text in the BOO stream.

Evidence from `BOO/packet.script` and the decoded `BOO/packet.boo` `EDITION`
topic:

| Source | BOO/token interpretation | BookServer/rendering interpretation |
| --- | --- | --- |
| `:coprnote.&copr. Evie Cooper 2026` | The `&copr.` source entity compiles to the CP500-table token word `0x00a9`. In the table used by the reader and `libgeist`, dictionary byte `0xb4` maps to `0x00a9`. | Render as the copyright sign before `Evie Cooper 2026`. |

The earlier experimental `libgeist` decoder incorrectly treated every
non-ASCII token word as the substitution character `?`. Later markup cleanup
then collapsed `?` as a separator/padding placeholder, so the copyright sign was
lost completely. The correct rule for the current single-byte fixture path is:

1. Emit printable ASCII token words `0x20..0x7e` directly.
2. Treat token word `0x00a0` as a normal space for Markdown/source projection.
3. Emit printable Latin-1 token words such as `0x00a9` as UTF-8.
4. Keep non-Latin-1 drawing/control-like token words as substitution until the
   full translation-table/output-control behavior is implemented. The CP500
   table also contains values such as box-drawing and arrow code points; in the
   packet cover/title records these are layout sentinels, not visible prose.

ASCII punctuation and symbol characters observed in the CP500 dictionary table
therefore survive the normal token path without special cases:

| Character | CP500 dictionary byte | Token word |
| --- | --- | --- |
| `@` | `0x7c` | `0x0040` |
| `#` | `0x7b` | `0x0023` |
| `&` | `0x50` | `0x0026` |
| `*` | `0x5c` | `0x002a` |
| `[` | `0x4a` | `0x005b` |
| `]` | `0x5a` | `0x005d` |
| `{` | `0xc0` | `0x007b` |
| `}` | `0xd0` | `0x007d` |
| `\` | `0xe0` | `0x005c` |
| `|` | `0xbb` | `0x007c` |
| `$` | `0x5b` | `0x0024` |

This means a renderer should not add entity-specific replacements for ordinary
punctuation such as `@`, `#`, `&`, `*`, brackets, braces, backslash, vertical
bar, or dollar sign. They are ordinary decoded token words after the code-page
table has been applied. Entity-like source constructs such as `&copr.` are a
BookMaster source concern; in the compiled BOO they appear as the corresponding
decoded character/token word.

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
- Complete field-level documentation of all logical-record iterator spacing and
  suppression controls beyond the verified low-valued prefixes above.
