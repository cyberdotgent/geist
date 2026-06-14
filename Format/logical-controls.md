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

## Character Conversion

After token resolution, the logical-record iterator concatenates token text
records into one word-counted 16-bit sequence. It inserts or suppresses spaces
according to small control words at the start/end of token records.

`sub_121ac63` then converts the 16-bit words to text. For the single-byte code
paths used by these fixtures, each word indexes a reader translation table and
usually emits one byte. The decoded output is NUL-terminated in memory.

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

The unresolved part is the complete dictionary delta/update grammar. The control
storage mechanism and the record/page framing are identified, but a standalone
implementation still needs the dictionary resolver before it can decode every
control value from scratch.
