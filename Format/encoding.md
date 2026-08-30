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
  fixtures that table is CP500. It is not the final display decoder.
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
- In the common single-byte path, output is the low byte of the table value.
  Values above `0x2fff` in the original token word, and decoded byte `0x1a`,
  are replaced with a substitution byte.
- Dictionary delta records can remap the current reconstructed token-word
  buffer before appending or changing case. For the bundled code-page-500
  samples, the normal/lowercase-oriented buffer mapping uses table numbers
  `(word >> 11) + 19`, and the uppercase-oriented buffer mapping uses
  `(word >> 11) + 13`. These transforms happen before final text output.
- For DBCS/stateful code pages (`933`, `935`, `937`, `939`), output carries
  `0x0e` and `0x0f` shift bytes around double-byte runs. Several multibyte code
  pages can emit full 16-bit table values. No repository fixture uses a
  multibyte code page, so these paths are recorded as unverified.
- Dictionary lookup kind `2` uses a separate reverse search-key encoding of
  token words rather than the output translation-table decoder.

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

Token boundaries stay explicit while this assembly happens: each token keeps its
own identity, assembled start column, and visible length, and the prefix rules
above act on the pending blank between tokens rather than on already-emitted
bytes. Consumers must therefore distinguish an actual two-character `ST` control
token from the same letters at the end of ordinary token text such as
`4302ABST`.

Evidence from `BOO/packet.boo` topic `COVER`. Offsets are absolute byte offsets
in the fixture file:

| `packet.boo` file offset | Token bytes | Resolved token text | Interpretation |
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

This casing is not a rendering convention that a converter may choose. It is
carried by the dictionary delta records themselves, so a decoder that ignores
them produces the wrong case. The delta modes, documented with their dictionary
framing in [logical-controls.md](logical-controls.md), are:

| Delta mode | Effect on the reconstructed token-word buffer | Note |
| --- | --- | --- |
| `0` | Apply the uppercase-table transform to the whole buffer, then apply the word-lowercase transform at each listed position. | Positions are stored against a buffer whose first word is a length, so a payload byte addresses word `index + 1`; a lengthless buffer uses the payload byte as a zero-based index. |
| `1` | Retain `count` words, read a further literal count, apply the normal-table transform to the retained words, then append the literal words. | The literal payload is copied after the transform. |
| `2` | Apply the normal-table transform to the whole buffer, then apply the word-uppercase transform at each listed position. | This is the common sentence-case path: normalize the token, then re-capitalize selected characters. |
| `3` | Apply the normal-table transform to the whole buffer, then append `count` literal words. | Same append path as mode `1`, without changing the retained length first. |

Each of these transforms is a lookup in a code-page-specific translation-table
page, selected by table group as described above. Verification is
end-to-end: decoding `QS3X36CM.BOO` with these rules reproduces the exact casing
the hosted BookServer serves for the same topics, including `System/36`,
`AS/400`, and `(CL)`.

## Character Classes And Special Characters

The decoding path documented in [logical-controls.md](logical-controls.md)
converts logical-record text in two stages: dictionary literal bytes first become 16-bit token words
through the selected code-page table, then the final text decoder emits display
bytes/characters from those token words. The bundled packet fixture uses the
CP500 dictionary literal table. That table includes ordinary ASCII punctuation
and Latin-1 symbols; these are not BookMaster entity text in the BOO stream.

Evidence from `BOO/packet.script` and the decoded `BOO/packet.boo` `EDITION`
topic:

| Source | BOO/token interpretation | BookServer/rendering interpretation |
| --- | --- | --- |
| `:coprnote.&copr. Evie Cooper 2026` | The `&copr.` source entity compiles to the CP500-table token word `0x00a9`; dictionary byte `0xb4` maps to `0x00a9`. | Render as the copyright sign before `Evie Cooper 2026`. |

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

## Hosted Display Bytes For The Non-ASCII Graphic Words

The hosted BookServer serves its pages as ISO-8859-1 and converts five of the
graphic token words the CP500 dictionary table produces into single 8-bit
display bytes. The mapping is a property of the BookServer output code page,
not of the book: it is the same in every fixture that draws with these words.

| Token word | CP500 dictionary byte | Character | Hosted display byte |
| --- | --- | --- | --- |
| `0x2190` | `0x2b` | `U+2190` left arrow | `0x1b` |
| `0x2191` | `0x14` | `U+2191` up arrow | `0x22` (`"`, served as `&quot;`) |
| `0x2192` | `0x2a` | `U+2192` right arrow | `0xff` (`ÿ` in ISO-8859-1) |
| `0x2193` | `0x15` | `U+2193` down arrow | `0x19` |
| `0x2666` | `0x04` | `U+2666` diamond bullet | `0xb0` (`°`) |

Evidence (`ACPZMST1` `1.2.5`, DT 19920319123146): the `FIGRSCOM2` box row is
served as `    Requester  | | Program  | \x1b___________\xff | Manager  | |
Server` and `FIGCOMP`'s riser rows as `              |   &quot;` /
`              \x19   |`; the `To summarize` bullets are served as `   \xb0   A
user program …`. The same five bytes appear in `SC09-138` `1.3.1`,
`SC34-425` `1.3.4`, `PRG1SORT` `1.1.2` and `DREICMST` `1.1.1.1`.

Two of the five bytes are what a CP437 console would draw for the character
(`0x1b` left arrow, `0x19` down arrow); the other three are not, so the table
is recorded as observed rather than derived.

The mapping is not stored in the book. Reading a table page at
`directory_page_number + (word >> 11)` — the page the table-selection formula
above names — lands on dictionary data in `ACPZMST1`, `SC09-138`, `PRG1SORT`
and `SC34-425`: the entry for the token word of `A` is not `0x41`, and the
entries for the arrow words are unrelated to the bytes hosted serves. No page
in any fixture holds this mapping. What is verified is the constant
per-character result across books.

`libgeist` deliberately emits the Unicode characters instead of these display
bytes. Its Markdown output is UTF-8, where `0x1b` is a control character and
`0xff` is not a valid encoding of `ÿ`; the arrow and bullet characters carry
the same meaning in one column each. The choice costs no column and no
character: over the whole corpus, all **564** preformatted lines in 141 topics
of 12 books that contain one of these five characters are byte-identical to the
hosted line once the table above is applied (`ACPZMST1`, `DREICMST`,
`FA1PLMM0`, `GC23-046`, `ITPPIBOK`, `PRG1SORT`, `SC09-138`, `SC24-546`,
`SC24-5520-00`, `SC28-1881-05`, `SC33-033`, `SC34-425`). A comparison against
hosted output should apply this table before diffing, exactly as a comparison
against a CP437 console would.

## The Container Applies No Compression

Body text is *tokenized*, not compressed, and the distinction matters when
first looking at a content page. Every step of decoding -- compact record
lengths, token references resolved through the directory token map, dictionary
base records with their delta and update records, translation-table pages, and
the inter-token spacing rules above -- is a table or index lookup. None of it
is entropy decoding, and there is no page-level transform to undo before a page
can be parsed.

Scanning the start of all 8,113 physical pages and all 623 resource payloads
across the repository fixtures finds no gzip, zip, bzip2 or `compress`
container signature. The handful of two-byte zlib-header coincidences such a
scan produces all fall inside legacy image payload areas whose framing is
accounted for by [GDF.md](GDF.md) and [MMR.md](MMR.md).

So the dense, `0x0000`-heavy runs inside content pages are packed BookManager
token structures rather than a compressed byte stream. They look binary because
a common word costs one or two bytes as a token reference.

Embedded legacy image payloads are the exception, and only within themselves:
kind `I` payloads carry CCITT fax coding ([MMR.md](MMR.md)). That compression
belongs to the image, not to the container, which stores the payload bytes
verbatim at an absolute file offset.

## Failure Mode To Avoid

Do not decode dictionary literal bytes with the CP500 table and then treat the
resulting low-valued token words as final ASCII or Unicode text. That loses the
table-backed normal/uppercase/lowercase transforms and bypasses the final BOO
translation-table pages. It also does not account for token-boundary spacing
during record assembly. The visible symptom is metadata such
as `as/400CommandCross-reference` or `CopyrightibmCorp.1991` instead of the
BookServer output `AS/400 Command Cross-Reference` and
`Copyright IBM Corp. 1991`.

## Open Questions

- Complete byte-for-byte behavior for every supported code page and fallback
  substitution path.
- Fixture evidence for dictionary index controls `2`, `4`, and `5`, which the
  control encoding implies but the repository fixtures did not need on the
  sampled root-to-terminal paths.
- Whether body text uses the same logical-record tokenization path throughout
  all page classes, or whether some page classes have additional text storage
  variants.
- Complete field-level documentation of all logical-record iterator spacing and
  suppression controls beyond the verified low-valued prefixes above.
