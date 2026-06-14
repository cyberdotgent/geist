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

Both fixtures do contain dense logical-record pages in the trailing `0x0001`
page run. These pages have a common record-page shape:

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
| `QS3X36CM.BOO` | pages 27-30 | page 27 `0x1000`, page 30 `0x025d` | page 27 has 729 records, page 30 has 141 records |
| `OFCUSEOV.BOO` | pages 88-98 | page 88 `0x0fe2`, page 98 `0x0753` | page 88 has 132 records, page 98 has 102 records |

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
  uint8_t control;       // Observed 0x03 in the root blocks.
  uint8_t unknown_01;    // Observed 0x00 in the root blocks.
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
after the key. The entry end is `entry_payload + entry_length`.

```c
struct BooDictionaryIndexEntryV2 {
  uint8_t compact_length[];      // Usually one byte in the observed root.
  uint8_t token_key_be[2];       // Extended token reference key.
  uint8_t cursor_payload[];      // Continuation/cursor bytes consumed by the
                                 // dictionary resolver after the key match.
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
| `OFCUSEOV.BOO` | `0x0e43` | 52 | `d7 45 2f 2d 81 00 02 03 96 86 00 01 01 00 01 3a 95 40 83 90 0c 85 00 01 02 81 3e 30 84 96 99 40 83 90 0f 85 40 a2 87 95 00 01 02 81 77 04 83 a2 a4 97 00 03` |
| `OFCUSEOV.BOO` | `0x0e78` | 19 | `da 8f 0e 83 88 81 95 87 85 40 a2 85 95 40 84 96 83 00 04` |
| `OFCUSEOV.BOO` | `0x0e8c` | 13 | `dd aa 08 85 94 40 a2 91 83 00 05` |
| `OFCUSEOV.BOO` | `0x0e9a` | 9 | `e1 6c 04 93 81 a2 a3 00 06` |
| `OFCUSEOV.BOO` | `0x0ea4` | 21 | `e5 23 10 97 99 89 a5 81 a3 85 40 83 90 0c 95 40 a2 87 95 00 07` |
| `OFCUSEOV.BOO` | `0x0eba` | 8 | `e9 74 03 a2 95 81 00 08` |
| `OFCUSEOV.BOO` | `0x0ec3` | 13 | `ed 18 08 a6 96 89 83 85 a2 00 09` |

The bytes after the token key are verified as the continuation/cursor payload
that positions the resolver for subsequent dictionary delta records. Their
subfields are still being separated, so independent readers should treat them
as resolver-controlled payload until more continuation examples have been
decoded.

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
| `0` | Transform the current reconstructed token buffer through a table. In `BooApplyDictionaryDeltaRecord`, optional following bytes are indexes into the current buffer; each indexed word is lowercased/mapped with `BooMapTokenWordToLower`. In the skip path, the cursor skips `count` payload bytes. |
| `1` | Start a new reconstructed token buffer with `count` existing words, then read a second six-bit literal count from the next byte and append that many literal words. |
| `2` | Transform the current reconstructed token buffer through the normal table, then apply optional indexed uppercase/mapping substitutions with `BooMapTokenWordToUpper`. In the skip path, this mode skips `count` payload bytes. |
| `3` | Transform the current reconstructed token buffer through the normal table, then append `count` literal words. This shares the append path used by mode `1`. |

Literal words are read in one of two forms:

| Dictionary text mode | Literal storage |
| --- | --- |
| Mode value `1` in the book handle | Literal words are big-endian 16-bit values. |
| Other observed version-2 path | Literal bytes index the translation table at handle offset `+3472`, producing 16-bit token words. |

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

## Reader Implementation Notes

An independent reader that wants these controls should not scan the BOO file for
the strings above. It should:

1. Parse page 0 and the directory page as described in
   [boo-header.md](boo-header.md).
2. Use the directory token threshold and token-map offset.
3. Parse logical-record pages as length-prefixed token-reference records.
4. Resolve token references through the token map and dictionary pages.
5. Convert the resolved 16-bit character-code records to bytes.
6. Compare the decoded strings with the control keys and stop at `CDOCNUM=`.

The remaining unresolved pieces are now narrower:

- the exact meaning of the dictionary block header byte at offset `+1`;
- the complete subfield layout of the dictionary index-entry continuation
  payload after the token key;
- the exact cursor-field layout in a clean public structure rather than the IBM
  reader's in-memory handle offsets;
- complete byte-for-byte reproduction of every code-page-specific output path.

The delta operation byte and reconstructed token buffer behavior are identified,
and the translation-table loader is identified. A standalone implementation
still needs the continuation-payload subfields to reproduce every extended-token
dictionary seek from scratch.
