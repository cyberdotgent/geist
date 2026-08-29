# Logical Book Header Controls

Book-level controls such as `CLANGUAGE=`, `CVERSION=`, `CTITLE=`, and
`CDOCNUM=` are not stored as raw ASCII or raw EBCDIC strings in the BOO files.
They are stored as tokenized logical records. The IBM reader reconstructs each
record by reading compact token references from logical-record pages, resolving
those references through the directory token table and dictionary pages, and
then converting the resulting 16-bit character codes to bytes before comparing
against control-key strings.

## Reader-Code Evidence

The relevant `ephwam.dll` path is:

| Routine | Address | Role |
| --- | ---: | --- |
| Logical control parser | `0x1217645` | Iterates decoded logical records and compares against `CLANGUAGE=`, `CVERSION=`, `CBLDVERS=`, `CTITLE=`, `CDOCNUM=`, and related keys. |
| Logical record iterator | `0x12217c6` | Concatenates the token payloads that form one decoded logical record. The first decoded record must start with `L` (`0x004c`). |
| Stream record reader | `0x121eee1` | Reads one length-prefixed encoded record and expands its token references into token descriptors. |
| Record-length helper | `0x1216189` | Reads compact record lengths: one byte for values `0x00..0xef`, or two bytes for long form. |
| Token resolver | `0x1218250` | Resolves a token descriptor to a word-counted 16-bit character-code record. |
| Extended-token resolver | `0x1218ac5` / `0x1218593` | Walks dictionary pages for token references at or above the directory token threshold. |
| Character-code decoder | `0x121ac63` | Converts the 16-bit character-code records to NUL-terminated text strings. |

The `ephwam.dll` IDB now has descriptive names on this path:

| IDA name | Address | Verified behavior |
| --- | ---: | --- |
| `BooReadCompactRecordLength` | `0x1216189` | Reads the one- or two-byte record length prefix. |
| `BooDecodeTokenReferenceNumber` | `0x1216247` | Converts a 2- or 3-byte token reference into a sequential token number. |
| `BooResolveTokenTextRecord` | `0x1218250` | Resolves a one-byte or extended token reference to a word-counted token text record, using the cache for one-byte token IDs. |
| `BooResolveExtendedTokenReference` | `0x1218ac5` | Seeks and advances dictionary state until the requested extended token record is reconstructed. |
| `BooSeekDictionaryTokenRecord` | `0x1218cef` | Searches dictionary index groups for the requested token key and leaves the cursor at the matching dictionary delta record. |
| `BooApplyDictionaryDeltaRecord` | `0x1218593` | Applies one dictionary delta/update record to the current reconstructed token-word buffer. |
| `BooSkipDictionaryTokenRecords` | `0x12188d8` | Advances the dictionary cursor across a requested number of delta records. |
| `BooResetDictionaryCursorForToken` | `0x1218b43` | Loads a base token text record at the current cursor and resets the reconstructed token buffer. |
| `BooMapTokenWordBufferNormalTable` | `0x121a0ea` | Maps a word-counted token buffer through the current translation table. |
| `BooMapTokenWordBufferUpperTable` | `0x1219f22` | Maps a word-counted token buffer through an alternate uppercase-oriented table. |
| `BooMapTokenWordToUpper` | `0x121a765` | Maps one token word, uppercasing ASCII `a..z` to `A..Z` in the simple path. |
| `BooMapTokenWordToLower` | `0x121a9e4` | Maps one token word, lowercasing ASCII `A..Z` to `a..z` in the simple path. |
| `BooLoadTranslationTablePage` | `0x121c31c` | Loads and caches a 4096-byte translation table page by table number. |
| `BooCompareTokenWordStrings` | `0x121611a` | Compares token-word strings while applying the reader's search collation rules. |
| `BooEncodeUnicodeWordsToSearchBytes` | `0x1219c94` | Encodes token words to the byte keys used by dictionary lookup kind `2`. |
| `BooEncodeUnicodeWordToSearchByte` | `0x1219265` | Encodes one Unicode/token word to a dictionary-search byte. |

The CGI IDB (`bookmgr.exe`) calls `Scm_Bopen` and `Scm_Binfo` from
`ephwam.dll`; it consumes the returned metadata but does not parse these
tokenized records itself.

## Fixture Evidence

Neither repository fixture contains the raw control keys as ASCII or EBCDIC
byte strings:

| Search key | `QS3X36CM.BOO` | `OFCUSEOV.BOO` |
| --- | --- | --- |
| ASCII `CLANGUAGE=` / EBCDIC CP037 `CLANGUAGE=` | not present | not present |
| ASCII `CVERSION=` / EBCDIC CP037 `CVERSION=` | not present | not present |
| ASCII `CTITLE=` / EBCDIC CP037 `CTITLE=` | not present | not present |
| ASCII `CDOCNUM=` / EBCDIC CP037 `CDOCNUM=` | not present | not present |

Both fixtures contain tokenized logical records in the `0x0000` content run and
also contain dense trailing `0x0001` logical-record pages. The book-header
control parser initializes logical stream 0 and reads from the content-stream
records first; an implementation that scans only the trailing `0x0001` run will
miss `CLANGUAGE=`, `CVERSION=`, `CTITLE=`, `CDOCNUM=`, and related controls.

The hosted BookServer instance provides a reader-output check for
`QS3X36CM.BOO`. Its contents page reports:

| Reader field | Hosted BookServer output |
| --- | --- |
| HTML title/address title | `AS/400 Command Cross-Reference` |
| Document number | `SX41-8209-00` |
| Build version | `1.2` |
| Copyright line | `Copyright IBM Corp. 1991` |

Any decoder that produces strings such as
`as/400CommandCross-reference` or `CopyrightibmCorp.1991` has not reproduced
the reader's token-word translation and logical-record spacing path.

The record pages have a common page shape:

```c
struct BooLogicalRecordPage {
  uint16_t page_class_be;       // 0x0001 in observed logical-record pages.
  uint16_t used_length_be;      // byte offset of end of used page data.
  uint8_t records[];            // length-prefixed records from offset 0x0004.
};
```

Observed logical-record pages:

| File | Page range | Used-length examples | Record count examples |
| --- | --- | --- | --- |
| `QS3X36CM.BOO` content stream | pages 7-26 | page 7 `0x0fef`, page 26 `0x0d4f` | page 7 begins with the header-control record. |
| `QS3X36CM.BOO` trailing logical run | pages 27-30 | page 27 `0x1000`, page 30 `0x025d` | page 27 has 729 records, page 30 has 141 records. |
| `OFCUSEOV.BOO` content stream | pages 10-86 | page 10 `0x0ffe`, page 86 `0x0615` | page 10 begins with the header-control record. |
| `OFCUSEOV.BOO` trailing logical run | pages 88-98 | page 88 `0x0fe2`, page 98 `0x0753` | page 88 has 132 records, page 98 has 102 records. |

The page-1 directory identifies the dictionary/token-table inputs used by the
resolver:

| Directory field | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Use in reader |
| ---: | ---: | ---: | --- |
| `0x0014` byte | `0xdc` | `0xd5` | Token threshold. Record bytes below this value are one-byte token IDs. |
| `0x0022` word | `0x0c8c` | `0x0c8c` | Offset of the two-byte token map used for one-byte token IDs. |
| `0x0026` word | `0x0e44` | `0x0e38` | Offset of the version-2 dictionary token-lookup index root in the directory page. |
| `0x0028` word | `0x0002` | `0x0002` | First dictionary/cache page loaded for token records. |
| `0x0034` word | `0x0e82` | `0x0ed2` | Offset of another in-page table used by the logical stream machinery. |
| `0x003c` word | `0x0068` | `0x0068` | Offset of the stream/page table. |
| `0x003e` word | `0x000a` | `0x00c9` | Stream/page table count. |

## Record Lengths

Records inside logical-record pages start at page offset `0x0004`. Each record
has a compact length prefix followed immediately by that many payload bytes.

The reader length helper at `0x1216189` implements:

```c
uint16_t read_record_length(uint8_t const **cursor) {
  uint8_t first = *(*cursor)++;
  if (first <= 0xef) {
    return first;
  }
  uint8_t second = *(*cursor)++;
  return ((uint16_t)(first - 0xf0) << 8) + second;
}
```

Examples:

| File | Page | Offset | Bytes | Length |
| --- | ---: | ---: | --- | ---: |
| `QS3X36CM.BOO` | 27 | `0x0004` | `06` | 6 |
| `QS3X36CM.BOO` | 27 | `0x000b` | `13` | 19 |
| `QS3X36CM.BOO` | 2 | `0x0004` | `f1 b0` | 432 |
| `OFCUSEOV.BOO` | 88 | `0x0004` | `33` | 51 |
| `OFCUSEOV.BOO` | 2 | `0x0004` | `f2 e9` | 745 |

The `0x0001` logical-record pages mostly use short one-byte record lengths. The
`0x0100` dictionary pages commonly use the long form.

## Encoded Record Payload

The payload of a logical record is a sequence of token references, not a
sequence of output characters.

For the version-2 fixtures:

1. Read one token-reference byte.
2. If the byte is below the directory token threshold (`0xdc` or `0xd5` in the
   fixtures), it is a one-byte token ID.
3. If the byte is at or above the threshold, read one following byte; the two
   bytes form a big-endian extended token reference.
4. The version-3 reader branch can use a three-byte extended token reference
   when the directory version is ` 1.3` and the extended-token flag/range is
   active.

The stream reader stores each token reference in an internal 8-byte descriptor.
The descriptor also records segmentation metadata used later when the iterator
assembles one decoded logical record from one or more token records.

### Display Lines Inside A Record Payload

Verified on the topic records of drawn (`SRFIG` without a picture) figures:
the token references of a record payload are grouped into display lines, each
introduced by one byte holding the byte length of the tokens that follow it:

```text
payload := line*
line    := length_byte token_reference{length_byte bytes}
```

The length byte is usually below the token threshold, so a token-reference
reader resolves it as a one-byte dictionary token whose spelling is unrelated
to the line (`call`, `command`, a box junction, a run of spaces).  Its value
is the line length, not a dictionary reference.  Evidence (`bootrace`/probe
token ordinals, encoded values, byte widths):

| File / record | Length byte | Following tokens | Sum of token bytes | Hosted line |
| --- | --- | --- | --- | --- |
| `FA1PLMM0.boo` record 37, token 35 | `0x0b` | tokens 36-43: 3 spaces, `U+250C`, 63 `U+2500`, prefix-only, 9 `U+2500`, prefix-only, `U+2510` | 1+2+1+2+1+1+1+2 = 11 | `    ____…____ ` (box top) |
| `FA1PLMM0.boo` record 37, token 44 | `0x05` | tokens 45-49: 3 spaces, `U+2502`, 63 spaces, 8 spaces, `U+2502` | 5 | `   \|` + 72 blanks + `\|` |
| `FA1PLMM0.boo` record 37, token 50 | `0x0b` | tokens 51-60: `cfont 16 7 3 24 10 3 35 7 3` | 11 | (font control, no line) |
| `FA1PLMM0.boo` record 37, token 61 | `0x15` | tokens 62-78: 3 spaces, `U+2502`, `The manual VSE/ESA … for`, 4 spaces, `U+2502` | 21 | `   \| The manual VSE/ESA Networking Support has planning information for     \|` |
| `FA1PLMM0.boo` record 38, token 0 / token 3 | `0x02` / `0x17` | `SREFIG` / 3 spaces, `Details about other manuals … are` | 2 / 23 | (control) / `   Details about other manuals available for VSE/ESA and its components are` |
| `GG24-4302-00.boo` record 262, token 0 | `0x05` | tokens 1-5 | 5 | blank box row |
| `GG24-4302-00.boo` record 262, token 6 | `0x1a` | tokens 7-25 (16-space dictionary word is the length byte itself) | 26 | `   \| ------…-\|----… SERV. CLASS PERIOD(S)` |
| `GG24-4302-00.boo` record 262, token 32 | `0x3a` (`call`) | tokens 33-75 | 58 | `   \| REPORT BY: POLICY=WSTPOL01 …` |
| `ACPZMST1.boo` record 55, token 0 | `0x1a` | tokens 1-20 | 26 | `    Requester  \| \| Program  \| …` |

Every record of the topics FA1PLMM0 `PREFACE.3`, ACPZMST1 `1.2.5`,
GG24-4302-00 `3.3.4` (records 261-265), SC09-138 `1.3.1`, SC34-425 `1.3.4`,
SH20-918 `FRONT_1.3`, GC23-046 `6.2`, DREICMST `1.1.1.1`, PRG1SORT `1.1.2`,
SC24-546 `3.4`, SC24-5520-00 `1.1.26`, QSYSNEWG `2.1`, SG24-204 `3.1`, and
IEAC6MST `1.4` parses this way with every line ending exactly on a token
boundary and the record ending exactly at a line end (no line spans a record).
Controls (`SHPREFACE.3`, `ctopicn 10`, `ST  Where to …`, `SRFIGFIGUNIQ1`,
`cfont …`, `SREFIG`) each occupy a line of their own; a zero-length line is an
empty display line.

Displayed line text is the line's token words in order with the assembler's
inter-token spaces (spacing prefixes applied), the space the assembler
inserts before the next length byte excluded.  For reflow-off content
(figure bodies) hosted BookServer shows exactly that text, one `<pre>` line
per display line, with box-drawing words rendered as `_` (`U+2500`), `|`
(`U+2502`, `U+2514`, `U+2518`, `U+2534`, `U+251C`, `U+2524`, `U+253C`) and
blank (`U+250C`, `U+2510`, `U+252C`); a full-width rule line without corners
directly after `SRFIG` or before the caption (the `frame=rule` frame,
SC09-138 `1.3.1`, GC23-046 `6.2`) is shown as an empty line.  The arrow
words `U+2190`-`U+2193` and the bullet `U+2666` reach the hosted page through
the book's display translation tables (`ÿ`, `"`, dropped, `°`), which
`libgeist` does not yet apply.

##### A Length Byte May Be At Or Above The Token Threshold

Nothing constrains the length byte to the compact range: a line of 0xdc = 220
bytes stores 0xdc as its length even in a book whose token threshold is 0xd6.
A left-to-right token reader then takes it for the first byte of a two-byte
dictionary reference, swallows the line's first content byte, and every
following length byte lands mid-token, so the payload no longer parses as
display lines at all.

| File / record | Byte | Value | Read left-to-right as | Correct reading |
| --- | --- | --- | --- | --- |
| `PRG1SORT.boo` record 47 (threshold 0xd6) | 0xe5ae | 0xdc | token 0xdc18, width 2, spelling `classification` | length 220; the next byte 0x18 is the line's first token, a 3-cell space run |

Hosted BookServer prints no `classification` anywhere in PRG1SORT `1.1.3.1`.
Twenty-six PRG1SORT topics carry such a line.  A decoder therefore has to be
prepared to re-read a record payload line by line -- one byte of length, then
exactly that many bytes of tokens -- and should do so only when the resulting
walk consumes every line exactly, since the plain walk is right everywhere
else.

This is the structure the Layout IR describes as a width-1 "marker slot"
followed by a "native origin": the marker slot is the next line's length
byte and the origin is simply the line's leading space token.  The
prose-reflow interpretation of those slots in [markup.md](markup.md) is
unchanged; this section records the byte-level fact behind them.

#### Display Lines Govern Reflowed Prose Too

The same structure decides the row model of ordinary reflowed prose, not
only of reflow-off figure bodies.  Four consequences, each checked against
hosted BookServer on at least two books:

* **The length byte is the row-control slot, always and only.**  No token
  inside a line is a slot, however much its token geometry looks like one,
  and the length byte is never the row's origin run or display text.  Its
  dictionary spelling is arbitrary, so it may read as a box-drawing run
  (`U+2500`), a bullet (`U+2666`), a space run, a word (`as`, `a`, `are`) or
  an ordinary `.`; hosted prints none of them.  Evidence: SC33-033
  `PREFACE.1` record 18 token 219 spells `U+2666` in front of the `c.cc 4`
  line and hosted (DT 19930422134757) shows no bullet there; SC33-033 `4.5`
  record 176 spells the length bytes of its three `SI` lines as three- and
  six-cell space runs; SC31-711 `3.1` record 46 spells the length bytes of
  the `nettl` log example as `as`, `a` and `are`, and hosted (DT
  19941010174546) keeps the example rows apart with none of those words;
  FA1PLMM0 `17.2.3.1` record 713 ends a line with the one-byte word `a`,
  which a geometry rule took for the slot -- hosted (DT 19910927114801)
  serves `available to a user; CEOS a subset ...`; DREICMST `1.1` ends a
  paragraph `... systems management.` and the next line's length byte spells
  `.`, which a flattening reader prints as a second full stop.
* **An empty display line is the paragraph break**, and it is the only one:
  hosted answers it with `<p>`.  Bare spacing markers inside a line that
  draws nothing (an `SI` entry, a body control line) are not breaks.
* **A display line whose whole visible content is one `c.<xx>` opcode and at
  most one operand is a body control line and draws nothing** -- `c.cc 12`
  (GG24-4302-00 `2.2.3`, DT 19950308184737), `c.cc 4` (SC33-033 `PREFACE.1`),
  a bare `c.cp` (DREICMST `1.5.6.3`, DT 19911219125856).  Such a line reaches
  a token reader as ordinary text only when the decoder lost the control
  boundary; the display-line structure is what restores it.
* **A display line whose whole visible content is `U+2500` rule words is the
  reader's horizontal rule.**  Hosted serves it as `<hr>` and prints no
  character of it: ACPZMST1 `COVER` (DT 19920319123146) and DREICMST `COVER`
  (DT 19911219125856) both draw the cover frame as two such lines and both
  hosted pages carry `<hr>` in their place.  This is distinct from the
  full-width rule line *inside* a figure frame described above, which hosted
  shows as an empty `<pre>` line.
* **The line's leading whitespace is the row's left margin, measured, not
  guessed.**  A `U+2502`/ASCII `|` change bar standing in that whitespace
  occupies one display column plus the assembler's space, and the space run
  that ends the whitespace is the row's origin run; the row therefore starts
  at the column of the line's first word.  Details and hosted citations in
  `markup.md`, "The three-column left margin".

#### A Control-Shaped Word Inside A Row Is Display Text

The flattened decoded string splits a segment wherever a control-shaped word
follows a marker or a `,`, and any identifier-shaped word beginning `SR`
classifies as a structural (anchor) control.  The display line disproves some
of those splits: a word with **another displayed word in front of it on its
own display line** stands inside a row, so it is that row's display text and
opens no control.

Over the 34 fixtures the 14,392 structural segments split as

| Shape | Segments |
| --- | ---: |
| opens its display line, nothing after it | 9,138 |
| opens its display line, text after it | 4,987 |
| the record's display lines do not parse | 61 |
| a displayed word in front of it | ~200 |

and every one of the last group is prose -- `SRVAPPS`, `SRVBLDS`, `SREPLACE`,
`SREF`, `SRCVPAC`, `SRPI`, `SRC1`, `SRCFILE`, `SRTF5` and friends -- while no
`SREFIG`, `SRFIG*`, `SRGLS`, `SRHDR*`, `SRLIS*`, `SRLEN`, `SRTBL` or `SRFTN*`
anchor is among them.

Worked examples.  SH12-565 record 282 display line 31 is `<length byte>
<three-cell origin> <U+2666> <two-cell gap> SRCVPAC`, the fourth of the five
items `LOGMODE / RUSIZES / PSNDPAC / SRCVPAC / SSNDPAC.`; hosted `4.3.5` (DT
19941206115523) serves all five as `   °   <name>`.  DREICMST record 430 line
[195,205) reads `       the command is saved in the SRC.` and the split cut
`SRC.` off as its own segment; hosted `2.8.3` (DT 19911219125856) prints the
abbreviation.  SC24-5527-02 spells `SRVAPPS` the same way in eight records.

The segment boundary itself is real -- the flattened string did split there --
so a consumer reads the segment's payload as body text in place.  The split
does consume the separator it fired on: SH12-565 `3.1.6` stores the example
command `F QH,F XY,SRV=(3,2,2)` and the comma before `SRV` is dropped with the
boundary.

## Token Resolution

A token reference resolves to a word-counted 16-bit character-code record:

```c
struct BooTokenTextRecord {
  uint16_t word_count_be_or_host;  // Reader stores/uses this as a count.
  uint16_t words[word_count];      // Character-code tokens, not raw bytes.
};
```

The resolver behavior is:

1. For a one-byte token ID, look at the token-cache page loaded from the first
   dictionary page (`directory offset 0x0028`, page 2 in both fixtures). If an
   entry is cached, use it.
2. If no cached entry exists, read the two-byte token map entry at
   `directory_base + directory[0x0022] + token_id * 2`.
3. That token map entry is itself an extended token reference. Resolve it
   through the dictionary-page resolver.
4. Cache the resulting word-counted token text record in the dictionary/cache
   page when there is room.
5. For an extended token reference, compute the sequential token number and walk
   the dictionary pages until that token's text record has been reconstructed.

The dictionary-page resolver supports delta/update records, which is why
dictionary pages are not a simple flat array of strings.

## Dictionary Pages And Delta Records

Dictionary text pages are observed as `0x0100` page-class pages. Their first
four bytes match the same page framing used by logical-record pages:

```c
struct BooDictionaryPage {
  uint16_t page_class_be;       // 0x0100 in both repository fixtures.
  uint16_t used_length_be;      // byte offset of end of used page data.
  uint8_t records[];            // compact length-prefixed dictionary blocks.
};
```

The dictionary block length prefix uses the same compact length function
documented above. Examples:

| File | Page | Offset | Bytes | Length |
| --- | ---: | ---: | --- | ---: |
| `QS3X36CM.BOO` | 2 | `0x0004` | `f1 b0` | 432 |
| `QS3X36CM.BOO` | 2 | `0x01b6` | `f2 98` | 664 |
| `OFCUSEOV.BOO` | 2 | `0x0004` | `f2 e9` | 745 |
| `OFCUSEOV.BOO` | 2 | `0x02ef` | `f2 fa` | 762 |

These page-2 blocks are dictionary text/delta containers. They are not the
version-2 token-lookup root itself. For the repository fixtures,
`BooSeekDictionaryTokenRecord` starts extended-token lookup in the directory
page at the offset stored in directory word `0x0026`.

The lookup root is a compact-length-indexed block:

```c
struct BooDictionaryIndexBlock {
  uint8_t control;       // Descent/page-jump control byte.
  uint8_t unknown_01;    // Observed 0x00 in the root and dictionary pages.
  uint16_t used_end_be;  // Block-relative end offset.
  uint8_t entries[];     // Compact-length-prefixed entries.
};
```

Observed version-2 root index blocks:

| File | Physical page | Offset | Header bytes | Interpretation |
| --- | ---: | ---: | --- | --- |
| `QS3X36CM.BOO` | 1 | `0x0e44` | `03 00 00 39` | Entries occupy directory offsets `0x0e48..0x0e7c`. |
| `OFCUSEOV.BOO` | 1 | `0x0e38` | `03 00 00 95` | Entries occupy directory offsets `0x0e3c..0x0ecc`. |

Each index entry begins with the same compact length encoding used for logical
and dictionary records. In the version-2 token-reference lookup path, the first
two payload bytes are the searchable extended-token key. The reader compares
exactly this key width, then leaves the dictionary cursor at the byte immediately
after the key when it reaches a terminal entry. The entry end is
`entry_payload + entry_length`.

```c
struct BooDictionaryIndexEntryV2NonTerminal {
  uint8_t compact_length[];      // Usually one byte in the observed root.
  uint8_t token_key_be[2];       // Extended token reference key.
  uint8_t prefix_length;         // Byte count of prefix_bytes in observed
                                 // non-16-bit text mode.
  uint8_t prefix_bytes[prefix_length];
  uint8_t continuation[];        // Either a BE16 page number or nested entries,
                                 // depending on the block control byte.
};

struct BooDictionaryIndexEntryV2Terminal {
  uint8_t compact_length[];
  uint8_t token_key_be[2];
  uint8_t delta_record_bytes[];  // Starts immediately after the token key.
};
```

Observed root entries:

| File | Entry offset | Length | Payload bytes |
| --- | ---: | ---: | --- |
| `QS3X36CM.BOO` | `0x0e48` | 6 | `dc 00 01 01 00 02` |
| `QS3X36CM.BOO` | `0x0e4f` | 11 | `df d0 06 83 89 97 88 85 99 00 03` |
| `QS3X36CM.BOO` | `0x0e5b` | 12 | `e3 d0 07 84 a2 97 84 85 a5 84 00 04` |
| `QS3X36CM.BOO` | `0x0e68` | 9 | `e8 24 04 96 97 85 95 00 05` |
| `QS3X36CM.BOO` | `0x0e72` | 14 | `ec 58 09 a2 95 84 a4 a2 99 94 a2 87 00 06` |
| `OFCUSEOV.BOO` | `0x0e3c` | 6 | `d5 00 01 01 00 02` |
| `OFCUSEOV.BOO` | `0x0e43` | 52 | `d7 45 2f 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 2d 00 03` |
| `OFCUSEOV.BOO` | `0x0e78` | 19 | `da 8f 0e 83 88 81 95 87 89 95 87 10 f1 10 f1 4b f6 00 04` |
| `OFCUSEOV.BOO` | `0x0e8c` | 13 | `dd aa 08 85 94 82 85 84 84 85 84 00 05` |
| `OFCUSEOV.BOO` | `0x0e9a` | 9 | `e1 6c 04 93 81 a2 a3 00 06` |
| `OFCUSEOV.BOO` | `0x0ea4` | 21 | `e5 23 10 97 99 96 83 85 84 a4 99 85 10 f2 10 f1 4b f1 f6 00 07` |
| `OFCUSEOV.BOO` | `0x0eba` | 8 | `e9 74 03 a2 95 81 00 08` |
| `OFCUSEOV.BOO` | `0x0ec3` | 13 | `ed 18 08 a6 96 99 92 93 96 81 84 00 09` |

For these root entries, `control=0x03`. The continuation is therefore a
big-endian page number after the prefix bytes:

| File | Key | Prefix length | Prefix bytes | Continuation |
| --- | --- | ---: | --- | --- |
| `QS3X36CM.BOO` | `dc 00` | 1 | `01` | page `0x0002` |
| `QS3X36CM.BOO` | `df d0` | 6 | `83 89 97 88 85 99` | page `0x0003` |
| `QS3X36CM.BOO` | `e3 d0` | 7 | `84 a2 97 84 85 a5 84` | page `0x0004` |
| `QS3X36CM.BOO` | `e8 24` | 4 | `96 97 85 95` | page `0x0005` |
| `QS3X36CM.BOO` | `ec 58` | 9 | `a2 95 84 a4 a2 99 94 a2 87` | page `0x0006` |
| `OFCUSEOV.BOO` | `d5 00` | 1 | `01` | page `0x0002` |
| `OFCUSEOV.BOO` | `d7 45` | 47 | `2d` repeated 47 times | page `0x0003` |
| `OFCUSEOV.BOO` | `da 8f` | 14 | `83 88 81 95 87 89 95 87 10 f1 10 f1 4b f6` | page `0x0004` |
| `OFCUSEOV.BOO` | `dd aa` | 8 | `85 94 82 85 84 84 85 84` | page `0x0005` |
| `OFCUSEOV.BOO` | `e1 6c` | 4 | `93 81 a2 a3` | page `0x0006` |
| `OFCUSEOV.BOO` | `e5 23` | 16 | `97 99 96 83 85 84 a4 99 85 10 f2 10 f1 4b f1 f6` | page `0x0007` |
| `OFCUSEOV.BOO` | `e9 74` | 3 | `a2 95 81` | page `0x0008` |
| `OFCUSEOV.BOO` | `ed 18` | 8 | `a6 96 99 92 93 96 81 84` | page `0x0009` |

The dictionary pages reached from the root have page class bytes `01 00`. The
seek routine treats the first byte (`0x01`) as the same index-block control and
the used-length word at page offset `0x0002` as the block end. With
`control=0x01`, the selected top-level entry's continuation is not a page
number; it is an in-entry subrange containing terminal entries:

```c
struct BooDictionaryPageIndex {
  uint8_t control;       // 0x01 in observed token dictionary pages.
  uint8_t unknown_01;    // 0x00.
  uint16_t used_end_be;  // Page used length.
  BooDictionaryIndexEntryV2NonTerminal top_entries[];
};
```

Examples of page-level top entries:

| File | Page | Entry offset | Key | Prefix length | Prefix bytes | Nested terminal-entry range |
| --- | ---: | ---: | --- | ---: | --- | --- |
| `QS3X36CM.BOO` | 2 | `0x0004` | `dc 00` | 1 | `01` | `0x000a..0x01b5` |
| `QS3X36CM.BOO` | 3 | `0x0004` | `df d0` | 6 | `83 89 97 88 85 99` | `0x000f..0x02af` |
| `QS3X36CM.BOO` | 4 | `0x0004` | `e3 d0` | 7 | `84 a2 97 84 85 a5 84` | `0x0010..0x0278` |
| `OFCUSEOV.BOO` | 2 | `0x0004` | `d5 00` | 1 | `01` | `0x000a..0x02ee` |
| `OFCUSEOV.BOO` | 3 | `0x0004` | `d7 45` | 47 | `2d` repeated 47 times | `0x0038..0x0296` |
| `OFCUSEOV.BOO` | 4 | `0x0004` | `da 8f` | 14 | `83 88 81 95 87 89 95 87 10 f1 10 f1 4b f6` | `0x0017..0x02a2` |

The terminal entries inside these ranges do not have a separate prefix section.
When the block control has counted down to zero, the matched entry's dictionary
delta record starts immediately after the two-byte key:

| File | Page | Terminal entry offset | Key | Delta bytes start | Entry end |
| --- | ---: | ---: | --- | ---: | ---: |
| `QS3X36CM.BOO` | 2 | `0x000a` | `dc 00` | `0x000d` | `0x0026` |
| `QS3X36CM.BOO` | 2 | `0x0026` | `dc 09` | `0x0029` | `0x0075` |
| `QS3X36CM.BOO` | 3 | `0x000f` | `df d0` | `0x0012` | `0x0068` |
| `QS3X36CM.BOO` | 4 | `0x0010` | `e3 d0` | `0x0013` | `0x0048` |
| `OFCUSEOV.BOO` | 2 | `0x000a` | `d5 00` | `0x000d` | `0x0030` |
| `OFCUSEOV.BOO` | 3 | `0x0038` | `d7 45` | `0x003b` | `0x0090` |
| `OFCUSEOV.BOO` | 4 | `0x0017` | `da 8f` | `0x001a` | `0x004f` |

In the observed non-16-bit dictionary text mode, the nonterminal continuation
cursor is:

```c
continuation = entry_payload + token_key_width + 1 + prefix_length;
```

The reader also has a 16-bit text-mode branch where the prefix consumes
`2 * prefix_length` bytes before the continuation cursor.

The block control byte determines how the continuation cursor is used:

| Control value at time of decision | Reader action |
| ---: | --- |
| `0` | Terminal match. Set the current dictionary delta range to `entry_payload + token_key_width .. entry_payload + entry_length`. |
| `1` or `2` | Descend into the selected entry's continuation subrange. The next scan is bounded by `continuation .. entry_end`; the control is decremented at the top of the next pass. |
| `3` | Read a big-endian 16-bit page number at `continuation`, load that page, and resume scanning at page offset `0`. |
| `4` or `5` | Descend into the selected entry's continuation subrange first; after one or two counted-down passes, control `3` performs the page jump. |

Controls `3` and `1` are verified in both repository fixtures. Controls `2`,
`4`, and `5` are present in the reader logic but were not needed by the sampled
version-2 root-to-terminal token paths above.

Once the seek routine finds the requested token key, it stores dictionary cursor
state in the book handle:

| Cursor field role | Reader behavior |
| --- | --- |
| Dictionary page number | Selects the active dictionary page buffer. |
| Current record offset | Points at the matched delta record inside that page. |
| End record offset | Bounds the current delta-record region. |
| Sequential token number | Tracks how many delta records have been applied. |
| Reconstructed token buffer | Word-counted 16-bit token text buffer used by the resolver. |

### Delta Operation Byte

`BooApplyDictionaryDeltaRecord` and `BooSkipDictionaryTokenRecords` both parse a
delta/update record from the current dictionary cursor. The first byte is split
into a two-bit mode and a six-bit count:

```c
uint8_t op = *cursor++;
uint8_t mode = op >> 6;
uint8_t count = op & 0x3f;
```

The verified operation behavior is:

| Mode | Observed behavior |
| ---: | --- |
| `0` | Transform the current reconstructed token buffer through `BooMapTokenWordBufferUpperTable`, then lowercase/map each indexed word with `BooMapTokenWordToLower`. The reader stores a leading length word in the buffer, so each payload byte is applied at `index + 1`; a lengthless implementation can use the payload byte as a zero-based word index. In the skip path, the cursor skips `count` payload bytes. |
| `1` | Start a new reconstructed token buffer with `count` existing words, read a second six-bit literal count from the next byte, transform the retained buffer through `BooMapTokenWordBufferNormalTable`, then append that many literal words. |
| `2` | Transform the current reconstructed token buffer through `BooMapTokenWordBufferNormalTable`, then apply optional indexed uppercase/mapping substitutions with `BooMapTokenWordToUpper`. The reader applies payload bytes at `index + 1` because of the leading length word. In the skip path, this mode skips `count` payload bytes. |
| `3` | Transform the current reconstructed token buffer through the normal table, then append `count` literal words. This shares the append path used by mode `1`. |

Literal words are read in one of two forms:

| Dictionary text mode | Literal storage |
| --- | --- |
| Mode value `1` in the book handle | Literal words are big-endian 16-bit values. |
| Other observed version-2 path | Literal bytes index the codepage table at handle offset `+3472`, producing 16-bit token words. For the repository fixtures this is the reader's CP500 table selected from directory word `0x004c` (`0x01f4`). |

The codepage table used for dictionary literals is not an output character
decoder. It maps compact dictionary bytes to the reader's internal token-word
space. Those token words still need the delta transforms above and the final
translation-table decode described below. Treating low-valued token words as
ASCII or Unicode text is an implementation shortcut and is not sufficient for
BookManager metadata strings.

The buffer transforms used by the delta modes are table-backed token-word
transforms:

| Transform routine | Table selection in the common single-byte path |
| --- | --- |
| `BooMapTokenWordBufferUpperTable` | For each word, table group `(word >> 11)` is loaded as table number `group + 13`; out-of-range groups use the substitution word. |
| `BooMapTokenWordBufferNormalTable` | For each word, table group `(word >> 11)` is loaded as table number `group + 19`; out-of-range groups use the substitution word. |
| `BooMapTokenWordToUpper` | Words below `0x00df` use the simple ASCII `a..z` to `A..Z` path when applicable; other words are mapped through the uppercase table path. |
| `BooMapTokenWordToLower` | Words below `0x00c0` use the simple ASCII `A..Z` to `a..z` path when applicable; other words are mapped through the normal/lower table path. |

For multibyte code pages the same routines select alternate table ranges
(`group + 65` for uppercase-oriented mapping and `group + 97` for normal
mapping). The repository fixtures use code page `500`, so the common
single-byte ranges above are the verified path for the bundled samples.

The reconstructed token buffer has this in-memory shape after each applied
delta:

```c
struct BooReconstructedTokenText {
  uint16_t word_count;
  uint16_t words[word_count];
  uint16_t zero_terminator;
};
```

This structure is in reader memory. The on-disk data stores only delta/update
records, not this expanded form.

## Character Conversion

After token resolution, the logical-record iterator concatenates token text
records into one word-counted 16-bit sequence. It inserts or suppresses spaces
according to small control words at the start/end of token records.

This spacing step is separate from dictionary reconstruction. A dictionary token
can decode to `AS/400`, `Command`, or `Copyright`, while the iterator decides
whether a blank belongs between adjacent tokens in the logical record. Missing
spaces in `CTITLE=`, `CSTITLE=`, `CCOPYRIGHT=`, or similar controls indicate
that the iterator's token-boundary spacing/suppression rules have not been
implemented, even if the underlying token strings are otherwise resolved.

`BooDecodeTokenWordsToText` then converts the 16-bit words to text. A token word
is split into a translation table number and an index:

```c
uint16_t word = token_words[i];
uint16_t table_no = (word >> 11) + 1;
uint16_t table_index = word & 0x07ff;
```

When the current table does not match `table_no`, the reader calls
`BooLoadTranslationTablePage(session, table_no, err)`.

The translation-table loader reads one complete 4096-byte page:

```c
physical_page = directory_page_number + table_no - 1;
file_offset = physical_page * 4096;
```

`directory_page_number` is the page-0 directory locator. It is `1` in both
repository fixtures, so table number `1` maps to physical page `1`, table
number `2` maps to physical page `2`, and so on. The loaded page is cached by
book-buffer id and table number.

The table entry is a big-endian 16-bit value at `table_page + table_index * 2`.
In the common single-byte output path, words at or below `0x2fff` are decoded
through the table and the low byte of the table value is emitted. Words above
`0x2fff`, and decoded byte `0x1a`, emit the reader's configured substitution
byte instead.

For DBCS/stateful code pages (`933`, `935`, `937`, `939`), the reader emits
shift-out byte `0x0e` before double-byte values and shift-in byte `0x0f` before
returning to single-byte values. For multibyte code pages including `932`,
`938`, `942`, `948`, `949`, `950`, and `1381`, table values above `0x00ff` can
be emitted as two-byte values rather than collapsed to the low byte.

Only after this conversion does the reader compare against literal keys such as
`CLANGUAGE=`, `CVERSION=`, and `CDOCNUM=`.

The complete text path is therefore:

1. Decode logical-record payload bytes into token references.
2. Resolve each reference through the directory token map or dictionary index.
3. Reconstruct dictionary token-word buffers, applying the normal/uppercase
   table transforms required by each delta record.
4. Assemble a logical record from token-word buffers, applying the iterator's
   spacing and suppression controls.
5. Decode the assembled token words through BOO translation-table pages to
   obtain display bytes.

## Logical Header-Control Sequence

The book-open path initializes the logical stream at stream number 0, then reads
logical records until metadata extraction is complete.

The first selected logical record must decode to a record beginning with
`L` (`0x004c`). If it does not, the reader treats the book open as invalid.

The parser then recognizes these decoded controls:

| Decoded control key | Value starts after | Reader behavior |
| --- | ---: | --- |
| `CLANGUAGE=` | 10 bytes | Stores book language metadata. |
| `CVERSION=` | 9 bytes | Stores version metadata if no build-version override has been seen. |
| `CBLDVERS=` | 9 bytes | Stores build/version metadata and overrides `CVERSION=`. |
| `CREFLOW=ON` or `CREFLOW=on` | 10 bytes | Enables the reflow flag. |
| `CTITLE=` | 7 bytes | Stores full title metadata. |
| `CSTITLE=` | 8 bytes | Stores short title metadata. |
| `CCOPYRIGHT=` | 11 bytes | Stores copyright metadata. |
| `CSECURITY=` | 10 bytes | Stores security metadata. |
| `CDATE=` | 6 bytes | Stores date metadata. |
| `CAUTHOR=` | 8 bytes | Stores author metadata; repeated authors are concatenated with two spaces while under the reader's size limit. |
| `CDOCNUM=` | 8 bytes | Stores document number metadata and terminates the header-control scan. |

### Body Controls Without A Boundary Byte

Most controls inside a topic record are preceded by a boundary byte that the
decoded projection renders as a placeholder. The SCRIPT page controls are not
always: `c.cp` (keep together), `c.cc` (conditional column) and `c.pa` (page
eject) can follow the previous run with only a compact separator token, or with
none at all. The control is still one whole dictionary token whose decoded
words spell `c.` plus the two-letter opcode.

| Book | Record | Tokens | Encoded (value/width) |
| --- | ---: | --- | --- |
| FA1PLMM0 | 353 | 71 `VSE`, 72 `,`, 73 `c.cp`, 74 attach, 75 rule run | 72 = 2/1 (words `0x0001`, `,`), 73 = 49655/2 |
| GG24-4302-00 | 613 | 17 `SREFIG`, 18 bullet glyph, 19 `c.cc`, 20 `20` | 19 = 52750/2, 20 = 193/1 |
| SC09-138 | 1482 | 45 `other`, 46 `.`, 47 `,`, 48 `c.pa` | 46 = 1/1 (words `0x0001`, `.`), 47 = 2/1, 48 = 2-byte |
| SC33-033 | 241 | 53 `,`, 54 `SREFIG`, 55 junction glyph, 56 `c.cc`, 57 `2i` | 56 = 53126/2 |

Two facts follow.

* The compact token whose dictionary expansion is the attach control `0x0001`
  followed by a single `,` is a **control separator**, not display text. The
  same token stands between `csummary` and `chdlevel` in FA1PLMM0 record 352.
  Hosted BookServer prints no comma for it: FA1PLMM0 6.1.2 serves the list row
  as `   °   PSF/VSE`, and SC09-138 8.3.1.8 serves `each other.` with nothing
  after it.
* The operand rule is the one already documented for `c.cp`: the operand is
  the token immediately adjacent to the opcode, with no spacing token between
  them (`c.cc` `20`, `c.cc` `2i`). Everything after it is display text.

The opcode cannot be recognised from the flattened decoded string. The byte
before it projects to `?` — which is equally the projection of the attach
control word, of a bullet glyph and of every box-drawing word — so a
string-level scan that requires a space, `=`, `,` or `.` around the opcode
cannot see the control at all.

The reverse mistake is just as easy. A **one-byte** token whose encoded value
is below the row-control limit (48) is the next display line's length byte
(see "Display Lines Inside A Record Payload"), whatever its dictionary
spelling. IBMMMSTR record 1244 token 139 has encoded value 31, width 1, and
spells `c.cc`; it stands after the `:` that closes
`Messages print at run-time when:` and before the three-cell origin run of the
row `1.  An error occurs ...`. Reading it as a control drops that colon and
merges two numbered rows. Only two-byte dictionary tokens are body controls.

### A Word Is A Control Only Where A Boundary Byte Precedes It

The decoded string is not enough to tell a control from a prose word that is
spelled like one, because the boundary byte before a control projects to `?`,
and so do the attach-control word, every box-drawing word and every unmapped
word. The boundary is a *token*:

| Book | Record | Control | Boundary token before it |
| --- | ---: | --- | --- |
| ACPZMST1 CONTENTS | 18 | `ST` (token 25) | token 24, one word U+2514 |
| ACPZMST1 CONTENTS | 18 | `ctocdef=0` (token 31) | token 30, one word U+2518 |
| SC24-546 14.0 | 961 | `SRHDRIRRR` | the compact separator (attach + `,`) closing `csourcefn DMSB1IRR` |
| QSYSINFO GLOSSARY | 756 | `SRGLS AFP` (token 190) | token 189, one word U+2666 |

Prose words that look like controls carry no such boundary:

| Book | Record | Word | Token before it | Hosted |
| --- | ---: | --- | --- | --- |
| PRG1SORT 1.1.5.1 | 80 | `SRCFILE` (token 2) | token 1, an 18-cell space run | `<tt>SRCFILE(LIBRAR2/FILE3)</tt>` |
| SC24-546 14.0 | 961 | `SRRCMIT`, `SRRBACK` | the preceding sentence word | `either SRRCMIT or SRRBACK.` |
| SC31-605 2.0 | — | `SRFILTER` | the preceding sentence word | `Use the SRFILTER command` |
| SH12-565 3.1.11 | — | `SRVPREF` | the preceding sentence word | `the SRVPREF initialization parameter` |

Unresolved: the last three are still resolved to `SR<id>` anchors by opcode
spelling alone, so the word disappears from the body and only the anchor
remains. The legacy renderer loses the same words.

### The Metadata Envelope Spans Records

The topic metadata run — `SH<id>`, `CTOPICN`, `CPARENT`, `CFORWARDLEVEL`,
`CBACKLEVEL`, `CSUMMARY`, `CHDLEVEL`, `CSOURCEFN`, then `ST` — is a run of
control segments, not a property of the topic's first logical record. The
encoder breaks the record wherever the payload page ends, so the run continues
in the next record. Observed break points, one per required control:

| Book | Topic | Records | Break after |
| --- | --- | --- | --- |
| GC28-183 | 2.3.5 | 163/164 | `CTOPICN` |
| SC41-485 | COMMENTS | 455/456 | `CBACKLEVEL` |
| QSYSINFO | 2.1.57 | 163/164 | `CSUMMARY` |
| SC34-425 | 1.5.5 | 241/242 | `CHDLEVEL` |
| ACPZMST1 | 5.7 | 289/290 | `CSOURCEFN` |

Sixty-nine topics in the corpus break this way. A reader must walk the segments
of the topic in source order and end the envelope where its controls end.

## Reader Implementation Notes

An independent reader that wants these controls should not scan the BOO file for
the strings above. It should:

1. Parse page 0 and the directory page as described in
   [boo-header.md](boo-header.md).
2. Use the directory token threshold and token-map offset.
3. Parse logical-record pages as length-prefixed token-reference records.
4. Resolve token references through the token map and dictionary pages.
5. Apply dictionary delta records, including their normal/uppercase/lowercase
   token-word table transforms.
6. Assemble logical records with the reader's token-boundary spacing and
   suppression rules.
7. Convert the resolved 16-bit token-word records to bytes through the BOO
   translation-table pages.
8. Compare the decoded strings with the control keys and stop at `CDOCNUM=`.

The remaining unresolved pieces are now narrower:

- the exact meaning of the dictionary block header byte at offset `+1`;
- the exact cursor-field layout in a clean public structure rather than the IBM
  reader's in-memory handle offsets;
- fixture evidence for reader-supported dictionary index controls `2`, `4`,
  and `5`;
- complete byte-for-byte reproduction of every code-page-specific output path;
- complete public documentation of the logical-record iterator's spacing and
  suppression control words beyond the observed metadata strings.

The delta operation byte and reconstructed token buffer behavior are identified,
the dictionary index continuation subfields are identified for the observed
version-2 fixtures, and the translation-table loader is identified.

## Payload Indexing And Encoded Token Identity

Logical-record numbers used by the topic directory and by this document are
one-based.  For each compact record, the byte range needed to reconstruct its
source tokens begins immediately after the compact length and ends immediately
after the payload.  It is therefore an end-exclusive file range; neither byte
of a long-form length belongs to it.

This was checked exhaustively for `BOO/SC31-711.boo` (SHA-256
`ac5dcb35e10f6e08107fc2e6e87420ad2652bf675c069eb2f4cb2606a5415700`).
The directory token threshold is `0xd8`.  Re-reading every indexed payload,
using one byte for a reference below `0xd8` and two big-endian bytes otherwise,
consumes exactly its indexed range and reproduces the original decoded logical
record.  Selected ranges used for the fixed-layout investigation are:

| Logical record | Payload file range | Payload bytes | Topic |
| ---: | --- | ---: | --- |
| 19 | `[0x0000b2e3, 0x0000b4be)` | 475 | `1.1` |
| 20 | `[0x0000b4bf, 0x0000b589)` | 202 | `1.1` |
| 21 | `[0x0000b58b, 0x0000b733)` | 424 | `1.2` |
| 22 | `[0x0000b735, 0x0000b8d6)` | 417 | `1.3` |
| 23 | `[0x0000b8d8, 0x0000ba30)` | 344 | `1.3` |
| 74 | `[0x0000ec86, 0x0000ee15)` | 399 | `2.4.4` |
| 75 | `[0x0000ee16, 0x0000eea5)` | 143 | `2.4.4` |

An implementation that needs source provenance must retain both the numeric
reference and its encoded width before dictionary resolution.  For example,
SC31-711 logical record 74 contains the one-byte reference `84`, which resolves
to token word `U+2502`, and the two-byte references `d8 22`, `d8 31`, and
`d8 35`, which resolve respectively to `U+250C`, 43 copies of `U+2500`, and
`U+252C`.  Replacing those references immediately with decoded text loses the
distinction between one-byte and extended source tokens and makes exact payload
reconstruction impossible.

This identity is a storage fact, not evidence that a token-reference number has
a universal semantic meaning.  Reference values are interpreted through the
book's own token map and reconstructed dictionary.  Likewise, an index into a
decoded record's token vector is record-local grouping metadata, not an on-disk
token identifier or a stable coordinate across records.
