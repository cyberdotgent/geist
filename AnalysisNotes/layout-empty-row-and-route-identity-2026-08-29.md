# Metric/renderer route identity and the Layout IR empty row (issue #58) — 2026-08-29

Two follow-ups raised by `prose-subtoken-spans-2026-08-29.md`: the coverage
metric and the export renderer built the typed-lowering topic identity twice,
and `extract_layout_ir` dropped display words into rows it then discarded.
The normative format fact derived here is in `Format/markup.md`, section
"A span disproves an empty row"; this note keeps the procedure and the
measurements.

## Metric integrity: one identity construction

`typed_route_inventory` built `TopicIdentityIR` from the book topic catalog's
header evidence (`TopicInfo`), while `TocEntry::markdown()` built it in
`document.cpp` from the `TopicData` that `find_topic_data` resolved for the
same contents entry.  Both projections carry the same six fields, but they are
resolved by two different lookups, so a title-sensitive rejection ("ST title
does not match the topic title" is the obvious one) could in principle differ
between the number and the export it claims to measure.

Both now call `make_topic_identity(const TocEntry&)`
(`libgeist/src/topic_identity.cpp`), which reads the contents entry's own
`id`, `title`, `heading_level`, `topic_number` and logical-record bounds --
the fields `build_table_of_contents` attaches from the topic table.  The
inventory also stopped skipping contents entries the catalog has no topic
header for, and instead counts them the way the renderer treats them: a topic
with no logical-record range has no typed route and is legacy
(`topic_identity_has_body`).  No corpus book has such an entry today, so the
denominator is unchanged.

### Proof

`boo2git --force` over all 34 fixtures with a temporary `GEIST_ROUTE_TRACE`
build that prints the route the renderer actually takes for every topic it
exports, compared per topic against `bootrace --coverage`:

| | inventory topics | renderer topics | agree | disagree |
| --- | ---: | ---: | ---: | ---: |
| before (`51a686e`) | 7,362 | 7,362 | 7,362 | 0 |
| after | 7,362 | 7,362 | 7,362 | 0 |

So the divergence was latent, not actual: no topic disagreed before the change
and none after, and per-topic coverage output is byte-identical across the
identity change (`typed=5720` either way).  The metric is now correct **by
construction** rather than by coincidence.  The baseline is not lowered.

## Layout IR: a span disproves an empty row

`extract_layout_ir` records a marker/origin boundary wherever a width-1 token
carrying a word is followed by a width-1 run of spaces.  QSYSINFO record 631
segment 15 stores `cfont 39 9 1 49 12 1 62 5 1` over
`___     --         SX41-9072        Automatic Installation Guide`; `Guide`
(token 226) and the space run behind it satisfy that test, the row the
boundary opens carries no visible text and is dropped, and tokens 226--228
end up owned by nobody -- the "visible token ... claimed by no block"
rejection of ~26 topics.

The previous agent's naive repair (drop any inner boundary whose row carries
no visible word) gained +14 topics but broke four families that read empty
rows as structure, and was reverted.  Its written spec -- condition the drop
on a `CFONT`/`CSELECT` span covering the marker's own columns -- is what is
implemented here, and the evidence supports it exactly as written.

The Layout IR does not model display columns, so the operand's columns and the
row's stored bytes are related by an unknown left-margin shift.  The shift is
therefore *derived*: lay the open row's stored text out from the marker slot
that opened it with the candidate word at its end, and look for a shift under
which one triple covers that word exactly and every other triple of the same
control lands on a whole display word of the same row.  The boundary is
withdrawn only when such a shift exists and is unique.  For the QSYSINFO row
the shift is 3 -- the documented three-column left margin -- and the three
triples land on `Automatic`, `Installation` and `Guide`.

Two decisions inside that rule:

- **A control payload that reaches the end of its record** carries its
  geometry into the leading text segment of the adjacent record, the same
  adjacency the display run itself is joined on.  Record 631's trailing
  `cfont 39 5 1 45 6 1 52 3 1 56 8 1 65 5 1` addresses the row
  `___     01         SC41-0036        Basic Backup and Recovery Guide` whose
  bytes are in record 632; without this, APPENDIX1.4.2.1's row stays broken.
- **The shift is never negative.**  A negative shift means the row's stored
  bytes run past the columns the operand names -- a decoder placeholder run in
  front of the first display cell is the usual cause.  Allowing it was tried
  and measured: it merges `Overall, how satisfied are you` with `with` in
  SC31-711 `COMMENTS`, which is what hosted serves for that line, but the
  questionnaire's cell model then loses the `?` of
  `the information in this book?` (`lazy_open_sc31_711_topics` catches it).
  Unproven, so it stays fail-closed; the wrapped
  `Programmer's` / `Guide` rows of QSYSINFO `APPENDIX1.4.2.1.7` are the other
  shape this leaves alone.

### Measured

- **Coverage 5,720 → 5,727 of 7,362 (77.7% → 77.8%)**, all in QSYSINFO
  (310 → 317: APPENDIX1.4.2.1.2, .3, .4, .5, .6, .10, .20).  No book loses a
  topic; per-topic coverage output is otherwise unchanged except that 13 more
  QSYSINFO topics whose rows the fix repairs now decline on the *next*
  rejection ("ST title does not match the topic title", a different slice) and
  the unclaimed-token class falls 48 → 35 topics.
- **Whole-corpus `boo2git --force`: 7 changed files, 0 added, 0 removed** --
  exactly the 7 moved topics.  No already-typed topic's export changed.
- **The four families that the reverted repair broke were re-verified
  explicitly** and all pass unchanged: `comment_delivery_ir_synthetic`,
  `comment_delivery_document_lowering_synthetic`, `topic_document_lowering`,
  `sc31_711_row_ownership`, `lazy_open_sc31_711_topics`.  SC31-711 `COMMENTS`
  and the NetView questionnaire rows are byte-identical to the pre-change
  build (checked by diffing `bootrace --ir` rows for records 543--544).

### Hosted verification of the 7 moved topics

Hosted QSYSINFO, DT 19910524120827, body words compared against the exported
Markdown of both builds:

| topic | hosted body words | legacy missing | typed missing |
| --- | ---: | ---: | ---: |
| APPENDIX1.4.2.1.2 | 51 | 38 | 2 (`Guide`, `Guide`) |
| APPENDIX1.4.2.1.3 | 64 | 50 | 1 (`Summary`) |
| APPENDIX1.4.2.1.4 | 78 | 64 | 1 (`Guide`) |
| APPENDIX1.4.2.1.5 | 60 | 42 | 1 (`Guide`) |
| APPENDIX1.4.2.1.6 | 52 | 37 | 1 (`Guide`) |
| APPENDIX1.4.2.1.10 | 54 | 41 | 1 (`Reference`) |
| APPENDIX1.4.2.1.20 | 40 | 27 | 1 (`1`) |

Legacy prints `[Table: TBLTBLUNIQ<n>]` and drops the whole publication table;
the typed route now reproduces it.

### Handed forward: the table row model drops the word the layout now owns

The residual missing word in each of those topics is **not** the Layout IR
defect.  The layout row is now correct (`bootrace --ir` shows
`GC41-9678        Publications Guide`), and the table region claims the token,
but the fixed-table row model places it as row *structure* instead of cell
text: QSYSINFO record 635 token 119 `Guide` is positioned at columns 38--42
while the cell `Publications` of the same row ends at column 36, because that
row's own marker/origin pair displaces it by seven columns relative to its
neighbours.  Hosted serves `<I>Publications</I> <I>Guide</I>` in one cell.
The same shape produces the `Summary`, `Reference` and `1` losses above.  A
table/row-geometry slice owns this; it is a cell-column mapping bug, not a
row-boundary one.

Also unchanged and still fail-closed: the `Programmer's`/`Guide` wrapped rows
(negative shift, above) and the 35 remaining "claimed by no block" topics,
most of which are a different shape entirely (a decoded control word such as
`cparent` or `chdlevel` standing in a table region).
