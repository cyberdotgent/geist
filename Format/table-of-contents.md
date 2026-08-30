# BOO Table Of Contents

BookManager stores the table of contents as normal tokenized logical topic
content. It is not a separate raw ASCII table and it is not generated solely
from a physical page directory.

The `topic_id` stored in each `CTOCE` is a public topic identifier. It matches
the target topic header's `SH<id>` value, but it is not itself a physical file
address. The seek address comes from the directory `0x003c` topic-start index
after resolving the id to a topic number. See [topics.md](topics.md) for the
topic/page storage model.

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

Books whose content-page index does not fit in the directory page store a
pointer root instead: the count word is `0` and the second word is the 1-based
logical page (relative to the directory page) holding the whole table at page
offset `0`, in the same `count, next, values` layout with `next` = `0`.

| File | Directory page | Root words | Table page | Table words |
| --- | ---: | --- | ---: | --- |
| `SC09-138.boo` | `0x056` | `0000 00d8` | `0x056 + 0xd8 - 1 = 0x12d` | `00c4 0000 0001 0010 001b 0025 ...` (196 values, last `0x0979`) |
| `SC34-425.boo` | `0x0db` | `0000 0108` | `0x0db + 0x108 - 1 = 0x1e2` | `00f4 0000 0001 000d 001b 0027 ...` (244 values) |
| `N2AH1MST.BOO` | `0x001` | `0000 012d` | `0x001 + 0x12d - 1 = 0x12d` | `011a 0000 0001 000f 001c 0028 ...` (282 values) |
| `SC24-5520-00.boo` | `0x020` | `0000 00c0` | `0x020 + 0xc0 - 1 = 0xdf` | `00ae 0000 0001 000d 0017 0024 ...` (174 values) |

Only pages in this directory-declared content run contribute topic logical
records. A page class alone is not sufficient. `GG24-4302-00.boo` declares 55
content pages beginning at logical page 12 (physical pages 63–117) and a total
of 782 logical records. Physical pages 120–131 also begin with class word
`0x0001` and contain compact token records, but they follow intervening
non-content pages and are outside the content-page index. Treating them as
additional topic content appends 1,304 fabricated records to the final
`COMMENTS` topic. Following the directory count instead gives the verified
topic range 779–782 (end-exclusive API bound 783).

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
ordinal is greater than the current table's `count_be`. Books with more than
248 topics use it: the root's second word is then the 1-based logical page
(relative to the directory page) of a continuation table with the same layout
at page offset `0`. See the chained-table evidence in [topics.md](topics.md).

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
| `CPARENT` | Parent topic identifier. The control is always present; the operand is absent for a top-level topic. |
| `CFORWARDLEVEL` | Next topic at the same navigation level. Control always present, operand may be absent. |
| `CBACKLEVEL` | Previous topic at the same navigation level. Control always present, operand may be absent. |
| `CSUMMARY a b c` | `a` and `c` are the topic's display-row count and are always equal; `b` is its direct-child count. In `CONTENTS` this makes `a` = `c` = the number of `CTOCE` entries (exact in all 34 fixtures). See [topics.md](topics.md#csummary-a-b-c). |
| `CHDLEVEL` | Heading/type marker; complete observed value list in [topics.md](topics.md#chdlevel-observed-values). |
| `ST` | Display title for the topic. |

The full nine-line envelope, its fixed order, and the anchor controls that may
be interleaved with it are documented in
[topics.md](topics.md#the-envelope-is-a-fixed-nine-line-sequence).

Examples:

| File | Page/record | Decoded header excerpt |
| --- | --- | --- |
| `QS3X36CM.BOO` | page 7, record 4, payload offset `0x044d`, length `0x00e3` | `SHcontents`, `CTOPICN 3`, `CSUMMARY 10 0 10`, `CHDLEVEL :toc`, `ST Table Of Contents` |
| `OFCUSEOV.BOO` | page 12, record 2, payload offset `0x0185`, length `0x0186` | `SHcontents`, `CTOPICN 17`, `CSUMMARY 201 0 201`, `CHDLEVEL :toc`, `ST Table Of Contents` |
| `OFCUSEOV.BOO` | page 11, record 8, payload offset `0x084d`, length `0x0153` | `SHpreface.5.1`, `CTOPICN 11`, `CPARENT preface.5`, `CHDLEVEL :h3`, `ST Help for Displays` |

## `CONTENTS` Topic Payload

The actual table-of-contents entries are stored as `CTOCE` controls inside the
`CONTENTS` topic. Every record of the topic parses into length-prefixed display
lines and every one of those lines holds exactly one control, so the display-line
walk is the complete and verifiable segmentation of the topic; see
[markup.md](markup.md#generated-contents-and-index-topics-as-display-line-control-records).
The topic begins with its nine-line metadata envelope, then seven `CTOCDEF`
lines, then one `CTOCE` line per displayed TOC entry.

### There Is No Standalone `ETOC` Control

Retired claim: earlier text here said "some books then include an end marker
decoded by the current experimental text path as `ETOC`, sometimes preceded by
layout/control text such as `CZ OFF`. An independent reader should stop TOC
entry parsing at this marker." That was the flattened decoded string being read
as if `CZ OFF ETOC` were two things.

Measured over the 34 `CONTENTS` topics: no book contains a standalone `ETOC`
control. Five books close the topic with the `CZ` dialect's region closer
`cz OFF ETOC 0 0` on a display line of its own — `GX27-3999-00.boo`,
`SC09-2417-00.boo`, `SC41-485.boo`, `XWEBDEMO.boo` and `packet.boo`, which are
exactly the books whose bodies use `CZ` directives at all. The other 29 books
end the `CONTENTS` topic after their last `CTOCE` line with no terminator of any
kind.

The reliable end of the TOC entry stream is therefore the end of the `CONTENTS`
topic's logical-record range, taken from the topic-start index. A reader must
not look for a marker, and must not read past `start(CONTENTS + 1)`.

For the same reason the earlier note that "`GG24-4302-00.boo` contains trailing
non-TOC payload after the `ETOC` marker that includes a false
`CTOCE 0 0 005E0000 ...`-looking sequence" is retired: `GG24-4302-00.boo` has no
`ETOC` at all, its `CONTENTS` topic ends with the ordinary entry
`ctoce 0 1 COMMENTS ITSO Technical Bulletin Evaluation`, and every one of its
display lines opens with a documented control. The false entry was an artifact
of reading beyond the topic bound in the flattened string.

Observed `CTOCDEF` controls, identical in **all 34** fixtures (238 lines = 34
books x 7 definitions, no variant anywhere):

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

Both numbers are derived from the target topic, and both derivations are exact
over all 7,412 `CTOCE` entries of the 34 fixtures:

* **`nesting` is the length of the target topic's `CPARENT` chain.** Exact in
  7,412 of 7,412 entries, no exceptions. Observed values `0`..`4`. BookServer
  indents two columns per level.
* **`toc_style` is a function of the target topic's `CHDLEVEL`**, with no
  exception anywhere in the corpus:

  | Target `CHDLEVEL` | `toc_style` | Entries |
  | --- | ---: | ---: |
  | `:H0` | `0` | 50 |
  | `:H1` and every front-matter form (`:COVER`, `:TOC`, `:VNOTICE`, `:NOTICES`, `:PREFACE`, `:INDEX`, `:GLOSSARY`, `:FIGLIST`, `:TLIST`, `:SOA`, `:TITLE`, `:BIBLIOG`, `:ABSTRACT`, `:ABBREV`) | `1` | 791 |
  | `:H2` | `2` | 2,454 |
  | `:H3` | `3` | 2,871 |
  | `:H4` | `4` | 1,206 |
  | `:H5` | `5` | 40 |

  `CTOCDEF=6` is defined in every book but no entry uses style `6`, because no
  topic in the corpus has `CHDLEVEL :H6`. Not every topic gets an entry: 697
  `:H3` topics, 775 `:H4` topics and all 1,617 `:MSGNO` topics are omitted from
  their book's TOC.

Retired claim: earlier text here said "although `CTOCDEF=0 ...` exists as a
style definition, verified displayed entries use nonzero style numbers." That
is wrong. Fifty entries in eleven books use style `0`, and they are displayed.
They are the book's "Part" divisions, and their target topics are exactly the 50
`CHDLEVEL :H0` topics of the corpus. Byte-level evidence, `DREICMST.boo`
`CONTENTS`: the display line `ctoce 0 0 1.0 Part 1.  Installing and Customizing
SLR`; hosted BookServer at DT `19911219125856` serves it as

```html
<a name="1.0">1.0</a>           <a href="1.0?DT=19911219125856"><strong>Part 1.  Installing and Customizing SLR </strong></a>
```

with a blank line before and after, i.e. bold at nesting 0, the same
presentation style `1` gets. `GC28-183.boo` (7 entries), `SC09-138.boo` (9),
`SC24-5520-00.boo` (8), `SH12-565.boo` (6), `SC09-2417-00.boo` (5),
`GG24-395.boo` (4), `SC34-425.boo` (3), `QSYSINFO.BOO` (3), `PRG1SORT.boo` (2)
and `SC26-457.boo` (1) carry the rest.

In hosted BookServer output, entries with styles `0`, `1` and `2` are rendered
as strong topic headings, while deeper style `3` entries such as `PREFACE.5.1`
are rendered as indented non-strong links. Style `5` occurs only in
`packet.boo` (40 entries).

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
5. Locate the topic whose `CHDLEVEL` is `:TOC` (equivalently, whose id is
   `CONTENTS`), using the topic-start index and topic headers to map topic
   numbers to IDs.
6. Split each of that topic's records into display lines
   (`<length byte><that many bytes of tokens>`), as documented in
   [logical-controls.md](logical-controls.md#display-lines-inside-a-record-payload).
   Each line holds exactly one control.
7. Skip the nine envelope lines, read the seven `CTOCDEF` lines, then parse each
   `CTOCE` line as `(nesting, toc_style, topic_id, title...)`. The title runs to
   the end of the display line, so its internal spacing survives verbatim.
8. Stop at the end of the topic's logical-record range. Do not look for a
   terminator control; 29 of 34 books have none.
9. Normalize display case through the same token-word translation and display
   rules used for other decoded logical text.

## Open Questions

- The complete semantics of the `CTOCDEF` numeric fields. Which style a `CTOCE`
  uses is now fully determined (see above), but the meaning of the individual
  operands of a `CTOCDEF` definition is not. The trailing operand of styles
  `2`..`6` (`0, 2, 4, 6, 8`) is not the rendered indent: hosted indents by two
  columns per `CTOCE` nesting level, not per style. The definitions are byte-
  identical in all 34 fixtures, so the corpus offers no differential evidence
  and this cannot be resolved without a book that varies them.
- Whether any BookManager variant stores an alternate generated TOC rather than
  literal `CTOCE` controls in the `CONTENTS` topic.
- Why `:H3`, `:H4` and `:MSGNO` topics are sometimes omitted from the TOC. The
  omission is not random (all 1,617 `:MSGNO` topics are omitted) but no stored
  field seen so far records the decision.
