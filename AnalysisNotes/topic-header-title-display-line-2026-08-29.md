# Topic header titles read off the `ST` display line (issue #58)

Fixes the defect diagnosed in
`AnalysisNotes/menu-label-header-title-2026-08-29.md` at its source. The
normative rule is in `Format/logical-controls.md`, "A Topic Title Is Its `ST`
Display Line"; this note carries the workflow, the hosted trail and the
measurement.

## What changed

`build_topics` (`libgeist/src/toc.cpp`) took the header title from the
flattened decoded record with
`extract_control_value_until_boundary(metadata, "st ")`, which

* starts at the first `st ` *anywhere* in the record, so a `CSOURCEFN` operand
  ending in `ST` (GC28-183 `IEAB5EST`, 20 topics) puts the opcode word into the
  title, and
* stops at the next `?c` / `?s` / `?e` / `?cz` decoder boundary, which is not
  where the display row breaks, so the title runs on through the topic's `SI`
  index terms and first body paragraphs.

It now calls `topic_header_title_of_record`
(`libgeist/src/topic_header_title.cpp`, new), which returns the visible text of
the `ST` control's display line.

## Workflow

`build/bootrace <book> <topic> --lines` dumps every display line of a topic
with its length byte, token range, per-column class string and hosted display
text; that is the whole diagnostic this slice rests on. For topics below TOC
depth (`bootrace` resolves ids through the TOC) a scratch probe over
`decode_logical_record_sources` + `record_display_lines` prints the same thing
for a bare logical record.

The rule was derived by sweeping all 10,503 topics of the 34 fixtures, scoring
each candidate rule by agreement with the book's own TOC projection, and
reading the disagreements:

| Rule | Titles agreeing with the TOC projection |
| --- | ---: |
| flattened `ST` payload run (before) | 441 of 10,503 |
| display line, opcode segment only | 6,927 |
| display line, absorbing display-text segments | 7,167 |
| + glued-marker rule (shipped) | 7,242 |

The intermediate rules were rejected on evidence, not on the score: a leading
one-cell punctuation token is a marker only when it is *glued* to the opcode
(GC23-046 `7.0` `ST| Chapter 7.  Online Books`), and is title text when a space
stands between (SC24-5520-00 `6.11.3` `ST  *ACCOUNT System Service`).

## Hosted trail

Book name and `DT` come from the hosted shelf listing crossed with each
fixture's own `booinfo` timestamp and document number; the resulting map is
exact for all 34 fixtures (for example `GC28-183.boo` -> `GC28-1830-02`
`DT=19930625102617`, `SC09-138.boo` -> `SC09-1384-00` `DT=19910321130500`).

Headings checked directly, each equal to the display-line title:

| Book | Topic | DT | Hosted heading |
| --- | --- | --- | --- |
| GC28-183 | 5.8.1.1 | 19930625102617 | `Multiple Destinations` |
| QSYSINFO | 2.1.21 | 19910524120827 | `SC09-1159, Languages:  System/38-Compatible COBOL User's Guide and` |
| GC23-046 | 7.0 | 19920330095121 | `Chapter 7.  Online Books` |
| ACPZMST1 | 5.4 | 19920319123146 | `/etc/inittab File Definitions` |
| ACPZMST1 | 5.7 | 19920319123146 | `/local/local.init.dir/Singl2multi File Definitions` |
| SC24-5520-00 | 6.11.3 | 19911011135123 | `*ACCOUNT System Service` |
| SH20-918 | 3.1 | 19910520154851 | `:ABSTRACT--Document Abstract` |
| SC09-138 | 4.7.1 | 19910321130500 | `__amrc` |
| SC09-138 | 8.1.1 | 19910321130500 | `#pragma Preprocessor Directive` |
| SC24-5527-02 | 6.3.7 | 19921218151459 | `Create an APPLY List from Two SRVAPPS Tables` |
| DREICMST | CHANGES.2 | 19911219125856 | `Changes for Version 3 Release 2` |
| GG24-4302-00 | 5.0 (menu) | 19950308184737 | item `5.5 Dynamic Update of IMS Type 2 SVC` |

Swept automatically against the hosted `<Hn>` heading (change-bar prefix
stripped): **all 120** titles that differ from their TOC projection agree with
hosted, and 246 of a random 250 of the rest agree exactly (the 4 are generated
`INDEX` pages whose hosted page carries no topic heading, not title
differences).

## Measurement

`build/bootrace <book> --coverage`, whole corpus: **6,843 -> 6,886 of 7,362**
typed (93.0% -> 93.5%). No book regressed. Rejection classes:

| Reason | Before | After |
| --- | ---: | ---: |
| raw menu label differs from canonical catalog title | 41 | 3 |
| ... beyond its compact terminal token | 5 | 0 |

Whole-corpus `boo2git --force` before/after: **no file added and none
removed** (`boo2git` names topic files from the topic id, not the title), 64
markdown files changed -- 43 topics moved legacy -> typed, 20 differ only in
the order of the anchor and the heading (the menu family emits the anchor
first, which is hosted's own `<a name="X"><H2>...</H2></a>` nesting), and one
is the corrected menu label above. Every README/CONTENTS page is byte
identical to the baseline.

## Residual

* Three menu topics still fail `raw menu label differs from canonical catalog
  title`: QSYSINFO `2.1` -> `2.1.21`, SC26-457 `3.14.2` -> `3.14.2.8`,
  SC33-033 `A.3` -> `A.3.3`. The first is the QSYSINFO shape from the other
  side -- the *label* carries the full title while the header carries the row.
  The other two are the same defect in the *menu label*: SC26-457 record 543
  item 17 reads `Deleting Generically Named Entries in a Catalog:  Example 8
  ----------`, the label run continuing past the row into a rule of dashes.
  Fixing those is a menu-label slice, not a header-title one.
* `topic_header_title_of_record` declines for SH12-565 `19-6639`, the only
  topic in the corpus whose metadata records carry no `ST` display line; the
  flattened extraction remains the fallback there.
* The TOC projection keeps its own defects, untouched here: a trailing stray
  token (`%`, `;`, `'`, `:MSGNO`, `[`, `//`, `----------`, `<BOOK>`, `$`,
  `*`) on about 22 entries, and a dropped trailing `?` on question-shaped
  titles (ACPZMST1 `1.1.1` "What is a logical unit?"). These are what the
  README/CONTENTS comparison against hosted still shows.
