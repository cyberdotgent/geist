# BOO Topic And Documentation Page Storage

BookManager documentation pages are stored as logical topics. A topic is a
range of decoded logical records, not a physical 4096-byte page and not a raw
SGML file. The table of contents points at topics by their public topic
identifier, while random access uses a directory topic-start index.

## Topic Addressing Model

A topic is addressed by number, not by scanning for a marker:

1. Topic number `n` (1-based) yields a start logical-record number from the
   topic-start index at directory offset `0x003c`.
2. The topic occupies logical records `[start(n), start(n + 1))`. For the last
   topic, the terminal bound is `total_logical_records + 1`, the value at
   directory offset `0x0036` plus one.
3. There is no topic-end marker in the record stream. Topic size is
   `start(n + 1) - start(n)`.

This is verified purely from fixtures. In the books tabulated under
[Directory Fields](#directory-fields), every value in the topic-start index
resolves to a decoded logical record that begins with an `SH<id>` topic header,
and the entry count matches directory field `0x003e`. The `SH12-565.boo`
counter-example below shows why the index, and not a scan for leading `SH`, is
authoritative.

## Directory Fields

The directory fields used for topic storage are:

| Directory field | Meaning |
| ---: | --- |
| `0x0034` | Offset in the directory page of the content-page logical-record index. |
| `0x0036` | Total logical-record count. |
| `0x0038` | Content logical-page count. |
| `0x003a` | First content logical page, relative to the directory page. |
| `0x003c` | Offset in the directory page of the topic-start index. |
| `0x003e` | Topic count. This matches decoded `CTOPICS`. |

The topic-start index at directory offset `0x003c` uses the same 16-bit paged
index format documented in [table-of-contents.md](table-of-contents.md). Each
table has the shape:

```c
struct BooU16IndexTable {
  uint16_t count_be;             // Values stored in this table; the root holds at most 248 (0x00f8).
  uint16_t next_page_be;         // 0 in the last table; otherwise the 1-based logical
                                 // page (relative to the directory page, like 0x003a)
                                 // holding the next table at page offset 0.
  uint16_t value_be[count];      // 1-based ordinal lookup uses value[ordinal - 1].
};
```

When the topic count at `0x003e` is at most 248 the root table is direct
(`next_page_be` is `0`). Larger books split the index into a chain: the root
holds the first 248 starts and `next_page_be` names the continuation page,
whose table starts at offset `0` of that page and repeats the same layout.
Continuation values keep ascending from the root's last value, a continuation
table may hold far more than 248 values (`IBMMMSTR.boo` stores the remaining
1429 in one table), and the chain is complete when exactly `topic count`
values have been read. Verified by
parsing every bundled book and matching every decoded `SH<id>` header record:

| File | Topics (`0x003e`) | Root | Continuation | Interpretation |
| --- | ---: | --- | --- | --- |
| `GG24-395.boo` | `0x013d` (317) | directory page `0x104`, offset `0x68`: `00f8 004e 0003 0004 ...`, last value `0x0256` (598) | page `0x104 + 0x4e - 1 = 0x151`, file offset `0x151000`: `0045 0000 0257 025a 025b ...`, last value `0x0338` (824) | 248 + 69 starts; 827 logical records. |
| `DREICMST.boo` | `0x0176` (374) | directory page `0x006`: `00f8 003e ...`, last value 495 | page `0x006 + 0x3e - 1 = 0x43`, file offset `0x43000`: `007e 0000 01f0 ...` (496 ...), last value 735 | 248 + 126 starts; 753 logical records. |
| `SC09-138.boo` | `0x0222` (546) | directory page `0x056`: `00f8 00d5 ...`, last value 908 | page `0x056 + 0xd5 - 1 = 0x12a`, file offset `0x12a000`: `012a 0000 038e ...` (910 ...), last value 2427 | 248 + 298 starts; 2428 logical records. |
| `IBMMMSTR.boo` | 1677 | directory page `0x001`: `00f8 ...`, last value 308 | one continuation table: `0595 0000 0135 0136 ...` (1429 values, 309 ...) | 248 + 1429 starts; a continuation table is not limited to 248 values. |

Every row of the table above was re-read from the fixture bytes during the
2026-08-30 documentation audit and every value in it still holds. The audit
additionally checked that the chain walk terminates with exactly `topic count`
values in all 34 fixtures, and that the sum of the 34 directory `0x003e` words
is 10,502.

The index is the authoritative topic-boundary evidence. In `SH12-565.boo`
logical record 906 (inside `BIBLIOGRAPHY.2`, records 902-906) begins with the
order number `SH19-6639` of a bibliography entry; it is not in the index and
BookServer renders it as the last line of `BIBLIOGRAPHY.2`. A reader that
detects topic headers by scanning decoded records for a leading `SH` word
would split a spurious topic there.

Two independent checks confirm the split is spurious: the record carries none
of the nine envelope controls documented above, and `SH12-565.boo`'s directory
`0x003e` is 296 while a scan-based enumeration of that book yields 297 topics.
(As of this audit `libgeist` still makes exactly this split; see
[README.md](README.md#where-the-code-disagrees-with-this-documentation).)

Observed topic-start roots:

| File | Directory offset | First words | Interpretation |
| --- | ---: | --- | --- |
| `QS3X36CM.BOO` | `0x0068` | `000a 0000 0002 0003 0005 0006 0007 0009 000a 006d 0087 0096` | `0x000a` topics; first topic starts at logical record `2`; terminal for topic 10 is read as topic 11 and resolves to `total_logical_records + 1`. |
| `OFCUSEOV.BOO` | `0x0068` | `00c9 0000 0003 0005 0009 0010 0016 0019 001b 001d ...` | `0x00c9` topics; first topic starts at logical record `3`. |

## Topic Header Record

The first decoded logical record in each topic range is the topic header. It
starts with `SH<topic_id>` and contains controls that describe the topic and
its relationship to neighboring topics.

| Control | Meaning |
| --- | --- |
| `SH<id>` | Public topic identifier. This is the identifier used by TOC entries and BookServer topic URLs. |
| `CTOPICN` | 1-based topic number. This should match the ordinal used in the topic-start index. Always carries an operand. |
| `CPARENT` | Parent topic identifier. The control is always written; the operand is absent for a top-level topic (624 of 10,502 topics). |
| `CFORWARDLEVEL` | Next topic identifier at the same navigation level. Always written; operand absent in 1,931 topics. |
| `CBACKLEVEL` | Previous topic identifier at the same navigation level. Always written; operand absent in 1,931 topics. |
| `CSUMMARY a b c` | Display-row and child counts; see below. |
| `CHDLEVEL` | Topic kind or heading level; see the complete observed value list below. |
| `CSOURCEFN` | Original source member/file name. Always carries an operand. |
| `ST` | Display title. Its first display line is the title; see [logical-controls.md](logical-controls.md#a-topic-title-is-its-st-display-line). |

### The Envelope Is A Fixed Nine-Line Sequence

The 34 `BOO/` fixtures declare 10,502 topics in total (the sum of their
directory `0x003e` words). Every one of them resolves an `ST` display line, and
in 10,501 of them the controls before it are exactly

```text
SH<id>
CTOPICN <n>
CPARENT [<id>]
CFORWARDLEVEL [<id>]
CBACKLEVEL [<id>]
CSUMMARY <a> <b> <c>
CHDLEVEL :<kind>
CSOURCEFN <name>
ST [<title>]
```

one control per display line, in that order, with no control omitted and none
repeated. The order never varies and the set never varies, so a reader can
consume the envelope positionally. Zero, one or more bare `SR<id>` anchor lines
may be interleaved; only three anchor opcodes occur in that position across the
corpus — `SRHDR<id>` (3,094 lines), `SRMSG <id>` (1,664) and `SRLEN [<name>]`
(273, in `SC24-546.boo`, `SC33-033.boo` and `SC34-425.boo` only).

The single exception is `SC09-138.boo` topic `FRONT`: `SH`, `CTOPICN`,
`CPARENT`, `CFORWARDLEVEL`, `CBACKLEVEL`, `CSUMMARY`, then eleven
`C.REV <label>` lines, then an empty `ST`. It carries no `CHDLEVEL` and no
`CSOURCEFN`, so a reader that requires either will lose that topic.

Any *other* record that looks like a topic header is a false split. The
canonical counter-example is `SH12-565.boo` record 906, described under
"Directory Fields" below: it begins with the order number `SH19-6639`, has no
envelope at all, and is not in the topic-start index.

`C.REV` is the SCRIPT revision-code control. It occurs on 22 display lines in
four books (`IBMMMSTR.boo` `PREFACE.6` record 19 is `c.rev PREF |`, declaring
the revision character used for that book's change bars) and is the only
envelope-position control outside the nine above.

### `CSUMMARY <a> <b> <c>`

Verified over all 10,502 declared topics:

| Field | Meaning | Evidence |
| --- | --- | --- |
| `a` | The topic's display-row count: the number of the topic's body display lines that are not control lines. | Reproduced for 10,432 of 10,502 topics with a simple "line does not open with a control opcode" classifier; the 70 residual topics are attributable to the classifier, not to the field. Strong hypothesis. |
| `b` | The number of topics whose `CPARENT` names this topic, i.e. its direct children. | Exact in 10,502 of 10,502 topics. Verified. |
| `c` | Equal to `a` in every topic. | Exact in 10,502 of 10,502 topics. Verified. |

The earlier note that "in `CONTENTS`, the first and third values match the topic
count" was a two-fixture sampling artifact and is retired. In `CONTENTS`,
`a` and `c` equal the number of `CTOCE` entries in that topic — exact in all 34
books — which is the same as the directory topic count only when every topic is
listed in the TOC. That holds in 21 of 34 books; in the other 13 the two
numbers differ, sometimes by a lot (`IBMMMSTR.boo` has 1,677 topics and
`CSUMMARY 60 0 60`, `SC34-425.boo` has 822 topics and `CSUMMARY 260 0 260`).

### `CHDLEVEL` Observed Values

Complete census over the 10,501 topics that carry the control:

| Value | Topics | Topic ids |
| --- | ---: | --- |
| `:H0` | 50 | Numbered "Part" topics. |
| `:H1` | 538 | Numbered topics. |
| `:H2` | 2,454 | Numbered topics. |
| `:H3` | 3,568 | Numbered topics. |
| `:H4` | 1,981 | Numbered topics. |
| `:H5` | 40 | Numbered topics. |
| `:MSGNO` | 1,617 | Numbered message-catalog topics. |
| `:COVER` | 29 | `COVER` |
| `:VNOTICE` | 32 | `EDITION` |
| `:TOC` | 34 | `CONTENTS` |
| `:INDEX` | 29 | `INDEX` |
| `:PREFACE` | 27 | `PREFACE` |
| `:NOTICES` | 21 | `NOTICES` |
| `:FIGLIST` | 18 | `FIGURES` |
| `:GLOSSARY` | 18 | `GLOSSARY` |
| `:TLIST` | 10 | `TABLES` |
| `:SOA` | 9 | `CHANGES` |
| `:TITLE` | 9 | `TITLE` |
| `:BIBLIOG` | 9 | `BIBLIOGRAPHY` |
| `:ABSTRACT` | 5 | `ABSTRACT` |
| `:ABBREV` | 3 | `ABBREVIATIONS` |

`:H6` does not occur in the corpus even though `CTOCDEF=6` is defined in every
book. Each front-matter value maps to exactly one topic id in every book where
it occurs, so the value is a reliable way to find `CONTENTS`, `INDEX` and the
rest without string-matching topic ids.

Examples:

| File | Decoded header evidence |
| --- | --- |
| `QS3X36CM.BOO` | `SHcontents ... CTOPICN 3 ... CHDLEVEL :toc ... ST Table Of Contents` |
| `QS3X36CM.BOO` | `SH1.0 ... CTOPICN 4 ... CHDLEVEL :h1 ... ST Introduction` |
| `OFCUSEOV.BOO` | `SHpreface.5.1 ... CTOPICN 11 ... CPARENT preface.5 ... CHDLEVEL :h3 ... ST Help for Displays` |
| `GG24-4302-00.boo` | `SHfigures ... CTOPICN 6 ... CHDLEVEL :figlist ... ST Figures` |

## TOC References Versus Storage Addresses

The TOC does not store physical page addresses. A `CTOCE` entry stores a target
topic id, and that id matches the target topic header's `SH<id>` value.

For example, `QS3X36CM.BOO` contains:

```text
CTOCE 0 1 1.0 Introduction
```

The target topic starts with:

```text
SH1.0 ... CTOPICN 4 ... CHDLEVEL :h1 ... ST Introduction
```

Therefore the TOC's first reference is a public topic id. The actual seek path
is:

1. Resolve the TOC `topic_id` to a topic number by scanning topic headers or
   using a prebuilt `SH` id map.
2. Use that topic number to read the start logical-record number from directory
   `0x003c`.
3. Read logical records from `start(topic)` up to, but not including,
   `start(topic + 1)`.

The hosted BookServer URL pattern is consistent with this. The known URL
`/BOOKS/QS3X36CM/CCONTENTS?...` addresses the `CONTENTS` topic by a
case-normalized `C` plus the `SHcontents` id. This URL evidence confirms the
public addressing role of the topic id, not the physical storage address.

## Independent Reader Algorithm

To enumerate and address documentation pages:

1. Parse page 0 and locate the directory page.
2. Parse directory fields `0x0034` through `0x003e`.
3. Decode the content logical-record stream with the token pipeline documented
   in [logical-controls.md](logical-controls.md) and [encoding.md](encoding.md).
4. Read the topic count from directory `0x003e`.
5. For each topic number `n`, read `start_n` from the topic-start index at
   directory `0x003c`.
6. Compute `end_n` as the start value for topic `n + 1`; for the final topic,
   use `total_logical_records + 1`.
7. Decode records in `[start_n, end_n)`.
8. Parse the first decoded record as the topic header and build an id map from
   `SH<id>` to topic number.
9. Resolve TOC and cross-reference targets through that id map, then seek by
   topic number.

## Open Questions

- The continuation-chain layout above is fixture-derived: four books, every
  value cross-checked against decoded `SH<id>` header records. No fixture in
  this repository needs a third table in the chain, so whether the chain can
  extend past one continuation is untested here.
- The exact BookServer URL normalization rules for ids containing punctuation
  and mixed case. The storage-level id match is verified; URL spelling is a
  server presentation detail.
