# BOO Table Of Contents

BookManager stores the table of contents as normal tokenized logical topic
content. It is not a separate raw ASCII table and it is not generated solely
from a physical page directory.

An independent reader should decode the content stream, locate the logical
topic whose header begins with `SHcontents`, then parse the `CTOCE` controls in
that topic. The same logical topic record also contains `CTOCDEF` controls that
define the presentation styles used by `CTOCE` entries.

## Reader-Code Evidence

The connected `ephwam.dll` IDB has these high-confidence names on the topic and
logical-record path:

| IDA name | Address | Verified behavior |
| --- | ---: | --- |
| `BooSelectTopicByNumber` | `0x122202e` | Implements `Scm_Loctopic`; selects a topic number, initializes the logical cursor, and seeks to that topic's start record. |
| `BooSeekTopicStartRecord` | `0x1221e53` | Loads the content page for a logical record number and walks compact-length records to set the current record payload bounds. |
| `BooGetTopicStartRecordNumber` | `0x1222310` | Looks up a topic's start logical-record number from the directory `0x003c` topic-start table. |
| `BooLookupPagedU16Index` | `0x1221c99` | Generic paged 16-bit index lookup used by the topic-start table and the content-page record index. |
| `BooLocateLogicalRecordByNumber` | `0x1221c1b` | Maps a logical record number to a content page using the directory `0x0034` content-page index. |
| `BooFindIndexOrdinalForRecordNumber` | `0x12221b7` | Binary-search helper over paged 16-bit indexes. |
| `BooSaveLogicalCursorState` | `0x12224e7` | Copies the 22-byte logical cursor state from the book handle. |
| `BooRestoreLogicalCursorState` | `0x12224d1` | Restores the 22-byte logical cursor state. |

`Scm_Qrytopic` returns the currently selected topic number from the cursor
state. `Scm_Szqrytpc` returns the directory topic count. `Scm_Getln` reads the
next decoded logical record for the selected topic through
`BooReadNextLogicalRecord`.

## Directory Indexes

The directory page contains two indexes that make topic lookup efficient:

| Directory field | `QS3X36CM.BOO` | `OFCUSEOV.BOO` | Meaning |
| ---: | ---: | ---: | --- |
| `0x0034` word | `0x0e82` | `0x0ed2` | Offset in the directory page of the content-page logical-record index. |
| `0x0036` word | `0x00f1` | `0x03f5` | Total logical-record count used by `Scm_Szbook` and topic bounds. |
| `0x0038` word | `0x0014` | `0x004d` | Content page count. |
| `0x003a` word | `0x0007` | `0x000a` | First content logical page; convert to a physical page with the directory-page base described in [pages.md](pages.md). |
| `0x003c` word | `0x0068` | `0x0068` | Offset in the directory page of the topic-start index. |
| `0x003e` word | `0x000a` | `0x00c9` | Topic count returned by `Scm_Szqrytpc`; matches decoded `CTOPICS`. |

### Content-Page Record Index

The index at directory offset `0x0034` starts with the content page count and is
followed by start logical-record numbers for each content page plus a terminal
total. It lets the reader map a logical record number to a physical content
page before walking compact-length records within that page.

Observed words:

| File | Offset | First words |
| --- | ---: | --- |
| `QS3X36CM.BOO` | `0x0e82` | `0014 0000 0001 000f 001c 0029 ... 00e5 00f1` |
| `OFCUSEOV.BOO` | `0x0ed2` | `004d 0000 0001 0011 0020 002e ...` |

For `QS3X36CM.BOO`, `0x0014` is 20 content pages and the terminal `0x00f1`
matches the directory total logical-record count. For `OFCUSEOV.BOO`,
`0x004d` is 77 content pages and the terminal total is `0x03f5`.

### Topic-Start Index

The index at directory offset `0x003c` maps topic numbers to start logical
record numbers. In the bundled fixtures the table fits directly in the
directory page. The reader uses `BooLookupPagedU16Index`, whose direct-table
layout is:

```c
struct BooU16IndexDirect {
  uint16_t count_be;
  uint16_t reserved_or_base_be;      // Observed 0 in the direct root.
  uint16_t value_be[count];          // 1-based ordinal lookup uses value[ordinal - 1].
};
```

The same lookup routine can follow continuation pages when the requested
ordinal is greater than the current table's `count_be`; that paged case is
implemented in the reader but was not needed for the sampled topic-start roots.

Observed first topic-start words:

| File | Offset | First words |
| --- | ---: | --- |
| `QS3X36CM.BOO` | `0x0068` | `000a 0000 0002 0003 0005 0006 0007 0009 000a 006d 0087 0096` |
| `OFCUSEOV.BOO` | `0x0068` | `00c9 0000 0003 0005 0009 0010 0016 0019 001b 001d ...` |

The topic counts match the decoded logical header controls:
`CTOPICS=10` for `QS3X36CM.BOO` and `CTOPICS=201` for `OFCUSEOV.BOO`.

## Topic Header Records

Each topic starts with a decoded logical header record. The important controls
for TOC reconstruction are:

| Control | Meaning |
| --- | --- |
| `SH<id>` | Topic identifier. BookServer uses this as the URL/topic anchor, preserving reader-normalized case such as `CONTENTS`, `PREFACE.5.1`, or `1.0`. |
| `CTOPICN` | Sequential 1-based topic number. |
| `CPARENT` | Parent topic identifier, if any. |
| `CFORWARDLEVEL` | Next topic at the same navigation level, if any. |
| `CBACKLEVEL` | Previous topic at the same navigation level, if any. |
| `CSUMMARY a b c` | Summary/count triplet. In the `CONTENTS` topic, the first and third numbers match the total topic count in the bundled fixtures. |
| `CHDLEVEL` | Heading/type marker such as `:toc`, `:h1`, `:h2`, `:h3`, `:cover`, or `:preface`. |
| `ST` | Display title for the topic. |

Examples:

| File | Page/record | Decoded header excerpt |
| --- | --- | --- |
| `QS3X36CM.BOO` | page 7, record 4, payload offset `0x044d`, length `0x00e3` | `SHcontents`, `CTOPICN 3`, `CSUMMARY 10 0 10`, `CHDLEVEL :toc`, `ST Table Of Contents` |
| `OFCUSEOV.BOO` | page 12, record 2, payload offset `0x0185`, length `0x0186` | `SHcontents`, `CTOPICN 17`, `CSUMMARY 201 0 201`, `CHDLEVEL :toc`, `ST Table Of Contents` |
| `OFCUSEOV.BOO` | page 11, record 8, payload offset `0x084d`, length `0x0153` | `SHpreface.5.1`, `CTOPICN 11`, `CPARENT preface.5`, `CHDLEVEL :h3`, `ST Help for Displays` |

## `CONTENTS` Topic Payload

The actual table-of-contents entries are stored as `CTOCE` controls inside the
`CONTENTS` topic. The `CONTENTS` topic begins with `CTOCDEF` controls followed
by one `CTOCE` per displayed TOC entry.

Observed `CTOCDEF` controls are identical in both bundled fixtures:

```text
CTOCDEF=0 1 0 2
CTOCDEF=1 1 0 2
CTOCDEF=2 0 0
CTOCDEF=3 0 2
CTOCDEF=4 0 4
CTOCDEF=5 0 6
CTOCDEF=6 0 8
```

The `CTOCE` syntax observed in the decoded `CONTENTS` topic is:

```text
CTOCE <nesting> <toc_style> <topic_id> <title>
```

The first number is the nesting depth used by BookServer for indentation. The
second number selects one of the `CTOCDEF` presentation styles. In the hosted
BookServer output, entries using styles `1` and `2` are rendered as strong
topic headings, while deeper style `3` entries such as `PREFACE.5.1` are
rendered as indented non-strong links.

Examples from `QS3X36CM.BOO` page 7 record 4:

| Decoded `CTOCE` | Hosted BookServer output |
| --- | --- |
| `CTOCE 0 1 Cover Book Cover` | `COVER` / `Book Cover` |
| `CTOCE 0 1 contents Table Of Contents` | `CONTENTS` / `Table of Contents` |
| `CTOCE 1 2 1.1 Displaying as/400 Commands Online` | indented `1.1` / `Displaying AS/400 Commands Online` |
| `CTOCE 0 1 a.0 Appendix.  as/400 Control Language Commands` | `A.0` / `Appendix.  AS/400 Control Language Commands` |

Examples from `OFCUSEOV.BOO` page 12 record 2:

| Decoded `CTOCE` | Hosted BookServer output |
| --- | --- |
| `CTOCE 0 1 Title Title Page` | `TITLE` / `Title Page` |
| `CTOCE 0 2 front1 Special Notices` | `FRONT1` / `Special Notices` |
| `CTOCE 1 2 preface.5 Related Online Information` | `PREFACE.5` / `Related Online Information` |
| `CTOCE 2 3 preface.5.1 Help for Displays` | indented `PREFACE.5.1` / `Help for Displays` |
| `CTOCE 0 1 1.0 Chapter 1.  Using Calendar Functions` | `1.0` / `Chapter 1.  Using Calendar Functions` |

## Independent Reader Steps

To read a BOO table of contents without using IBM binaries:

1. Parse page 0 and the directory page.
2. Use directory `0x0034`, `0x0036`, `0x0038`, and `0x003a` to understand the
   content logical-record page index.
3. Use directory `0x003c` and `0x003e` to read the topic-start index.
4. Decode logical records from the content stream with the token/dictionary
   pipeline in [logical-controls.md](logical-controls.md).
5. Locate the topic header whose decoded record begins with `SHcontents`, or
   use the topic-start index and topic headers to map topic numbers to IDs.
6. Parse the `CTOCDEF` controls in the `CONTENTS` topic.
7. Parse each `CTOCE` control as `(nesting, toc_style, topic_id, title)`.
8. Normalize display case through the same token-word translation and display
   rules used for other decoded logical text.

## Open Questions

- The complete semantics of all four `CTOCDEF` numeric fields.
- Whether books with very large topic counts use the paged continuation branch
  of `BooLookupPagedU16Index` for the topic-start table.
- Whether any BookManager variant stores an alternate generated TOC rather than
  literal `CTOCE` controls in the `CONTENTS` topic.
