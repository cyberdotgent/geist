# BOO Topic And Documentation Page Storage

BookManager documentation pages are stored as logical topics. A topic is a
range of decoded logical records, not a physical 4096-byte page and not a raw
SGML file. The table of contents points at topics by their public topic
identifier, while random access uses a directory topic-start index.

## Reader-Code Evidence

The connected `ephwam.dll` IDB verifies the topic access path:

| IDA name | Address | Verified behavior |
| --- | ---: | --- |
| `BooSelectTopicByNumber` | `0x122202e` | Selects a 1-based topic number, stores it in the book cursor, reads the topic-start logical-record number, and seeks the logical cursor there. |
| `BooGetTopicStartRecordNumber` | `0x1222310` | Reads directory `0x003c` with `BooLookupPagedU16Index`; for `topic_count + 1`, returns `total_logical_records + 1` as the terminal bound. |
| `BooSeekTopicStartRecord` | `0x1221e53` | Positions the decoded-record cursor at the selected logical record. |
| `Scm_Loctopic` | `0x121cb89` | Public reader entry that calls `BooSelectTopicByNumber(runtime, handle, topic_number)`. |
| `Scm_Getln` | `0x121d526` | Returns the next decoded logical record from the selected topic cursor. |
| `Scm_Sztopic` | `0x121e12a` | Computes topic size as `start(topic + 1) - start(topic)`. |
| `BooFindTopicControlValue` | `0x121f636` | Selects the current topic and scans decoded topic records for a named control such as `SH`, then restores the caller's cursor. |
| `BooGetCurrentTopicIdFromHeader` | `0x121f7d6` | Retrieves the current topic id from the topic header's `SH` control. |

`Scm_Sztopic` is the strongest boundary evidence: the reader does not search
for an end marker to size a topic. It subtracts adjacent entries in the
topic-start table.

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
index format documented in [table-of-contents.md](table-of-contents.md). In the
sampled books the root table is direct:

```c
struct BooU16IndexDirect {
  uint16_t count_be;
  uint16_t reserved_or_base_be;  // Observed 0 in direct roots.
  uint16_t value_be[count];      // 1-based ordinal lookup uses value[ordinal - 1].
};
```

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
| `CTOPICN` | 1-based topic number. This should match the ordinal used in the topic-start index. |
| `CPARENT` | Parent topic identifier, if present. |
| `CFORWARDLEVEL` | Next topic identifier at the same navigation level, if present. |
| `CBACKLEVEL` | Previous topic identifier at the same navigation level, if present. |
| `CSUMMARY` | Summary/count triplet. In `CONTENTS`, the first and third values match the topic count in sampled fixtures. |
| `CHDLEVEL` | Topic kind or heading level, such as `:toc`, `:h1`, `:h2`, `:h3`, `:cover`, `:preface`, `:figlist`, or `:tlist`. |
| `ST` | Display title. |

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

- Whether very large books use the paged continuation branch of
  `BooLookupPagedU16Index` for the topic-start root. The reader implements this
  case, but sampled topic-start roots have been direct.
- The exact BookServer URL normalization rules for ids containing punctuation
  and mixed case. The storage-level id match is verified; URL spelling is a
  server presentation detail.
