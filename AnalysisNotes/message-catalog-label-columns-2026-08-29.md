# Section-label message catalogs beyond SC31-711 (2026-08-29)

Workload: widen the typed message/trap catalog families to the 55 corpus
topics that the prose family declines with `body control SRMSG is outside the
prose model`. Tracker: GitHub issue #58.

## Which family

Two typed families read an `SRMSG` topic.

- `MessageTopicIR` (`message_topic_ir.cpp`) models SC31-711's numeric
  `Meaning:`/`Action:` catalog. Its envelope is fixed: a numeric catalog
  boundary plus that two-label vocabulary. Over the 55 topics it declines 34
  with `source is not one numeric message catalog` and 21 with `source is not
  a complete Meaning/Action message envelope`.
- `TrapCatalogIR` (`trap_catalog_ir.cpp`) already discovers its label
  vocabulary from the catalog itself: the ordered run of `<label>:` lines that
  every entry repeats, read off the CFONT spans of each entry's own rows.

The trap family's envelope is the general one, so this workload extended it
and left `MessageTopicIR` alone. Nothing was routed by book or by label
spelling; every rule below is read off the source.

## What the declines turned out to be

Measured over the 55 topics with `bootrace <book> <topic> --ir`, `--lines`
and `--tokens`.

### 1. `trap catalog header contains an unsupported control: c.sp` (20)

Not a misread length byte. SC34-425 record 1476 line 10 is the whole display
line `c.sp 1 c`: the length byte is token 60 (encoded value 5, width 1), the
opcode is token 61 (value 45628, **width 2**), and tokens 62-63 are its
operand. `c.sp` is the SCRIPT vertical-space directive and draws nothing. The
same control also stopped `extract_message_prose_paragraphs_ir`, which reads
the catalog's introduction, with `message prose envelope contains a non-prose
control`. Both now admit a spacing segment after proving it owns no
`visible_content` cell, and the prose walk treats its whole token run as
control text (opcode *and* operand) so `1 c` cannot leak into a paragraph.

`cselect` is the other non-drawing control, but only sometimes: N2AH1MST
record 252 line 10 is the whole line `cselect 34 4 FTNFTNUNIQ17` marking the
`(*)` footnote hotspot drawn by line 11, yet the flattened splitter gives the
segment the *following* lines' text as its payload (segment 4, payload
[556,821)). It is therefore admitted as a non-drawing control only when it
owns no visible cell, and otherwise read as a text continuation.

### 2. `trap entry has no labelled field` (15)

The label vocabulary was discovered from the catalog, but the *column* it
starts at was not: `leading_chain` required every chain to open at the single
catalog origin column, which SC31-711 satisfies because its headlines and its
labels are both `cfont 3 …`. That is a property of one book.

N2AH1MST 4.0 (DT `19910329000100`), record 281:

| Line | Control | Row | Meaning |
| ---: | --- | --- | --- |
| 13 | `cfont 3 7 2 11 8 2 20 10 2 31 9 2` | `   AMA100I AMASPZAP PROCESSING COMPLETED` | headline at column 3 |
| 16 | `cfont 10 12 2` | `          Explanation:  This message occurs …` | field label at column 10 |
| 22 | `cfont 10 7 2` | `          Source:  SPZAP` | |
| 25 | `cfont 10 6 2 17 7 2` | `          System Action:  The job step ends.` | |
| 28 | `cfont 10 6 2 17 10 2 28 9 2` | `          System Programmer Response:  Check …` | |

Hosted serves exactly that: `<B>Explanation:</B>`, `<B>Source:</B>`,
`<B>System</B> <B>Action:</B>`, `<B>System</B> <B>Programmer</B>
<B>Response:</B>`. The catalog now carries a **label column** discovered the
same way the vocabulary is — from the first labelled line — and required of
every later label. Where the two columns differ the column decides whether a
highlighted run is the headline or a label, so a headline that legitimately
ends in `:` (record 2315 line 24, `IDC498D ACCESS REQUESTED TO text
VOL=SER=volser: REPLY Y OR N text is:`) is no longer mistaken for one.

The vocabulary stays the *intersection*: N2AH1MST 4.0 repeats
`Explanation:`, `Source:`, `System Action:` in every entry, while
`System Programmer Response:` (some entries answer `Operator Response:`)
remains an entry-local field.

### 3. `trap entry font spans do not map onto its row text` (12)

The chain rule assumed spans follow one another across exactly one space.
Two counter-shapes, both settled by the row's own display text:

- **No gap at all.** Record 2280 line 12 is `cfont 3 8 2 12 7 2 19 3 V` over
  `   IDC0014I LASTCC=cde`: the `LASTCC=` span (column 12, length 7) ends at
  column 19 where the variable `cde` begins, because the headline word is
  `LASTCC=cde`.
- **Two spaces.** Record 305 line 26 is `cfont … 20 6 2 28 5 2 …` over
  `   AMA133I CHECKSUM ERROR.  NO-GO SWITCH SET` — SCRIPT's sentence spacing.

`leading_chain`/`map_chain` were replaced by one `map_leading_chain` that
assumes nothing about the gap width and instead requires the display text
*between* two consecutive spans to be spaces. It returns the longest valid
opening run, so a highlighted word later in a field's text ends the run
rather than failing the whole line.

### 4. Layout glyphs a marker rule ate (part of 3 and of `headline is not fully highlighted`)

Three separate glyph losses, all inside a column the row's own CFONT
highlights:

- Record 2171 line 10, `   ICT1033 CRYPTOGRAPHY CIPHER FUNCTION FAILED -  CODE
  xx` with `cfont … 47 1 2 …`: LayoutIR calls the `-` (token 108, width 1,
  followed by a space run) a marker slot, and `marker_role` drops it as
  layout. Hosted serves `<B>-</B>`.
- Record 197 line 5, `   AHL040 NOT A LEGAL FORM OF THE MACRO. CHECK THE MF=`
  with `cfont … 51 3 2`: the trailing `=` is its own one-byte token at the
  segment end, which the terminal-glyph rule withholds. It is the third
  character of the highlighted word `MF=`.
- Record 151 line 13 ends `… SYNTAX ERROR ERROR =` with a final `cfont … 75 1
  2` over the `=`; hosted prints `<B>ERROR</B> <B>=</B>`.

Both rules now take the segment's own span list as the counter-evidence: the
walk is given `highlighted_columns` (the display width the spans reach from
the first span's column) and keeps a glyph that falls inside it. The
display-line length byte remains the only true row-control slot.

### 5. `trap entry text precedes its headline` / `contains an unsupported control` (13)

Every one is the display-line length byte again, in three shapes:

| Shape | Evidence |
| --- | --- |
| the whole segment is the byte | record 2365 token 0, value 37, spells `cfont`; the real control is token 1 |
| the byte is the segment's *opcode* and the row text follows it | record 2400 token 95, value 37, spells `cfont`; tokens 96-120 are the headline `   IDC01551I type CACHING STATUS: stat FOR SD X'ss' DEV X'dd''` |
| the byte opens a leading text segment | record 2284 token 0, value 31, spells `are`; it opens the 31-byte line `   IDC0064I text UPDATED IN CARTRIDGE LABELS AND INVENTORY RECORD` |

This is the finding already recorded in `Format/logical-controls.md`
§"A Metadata Opcode In The Body Is A Display-Line Length Byte", except that
the words involved here are ordinary dictionary words (`are`) and `cfont`,
not metadata opcodes. The trap family now indexes every display line's prefix
token and (a) never draws it, (b) skips a segment made only of such tokens,
and (c) reads a segment whose *first* token is one as that row's display
text. Shape two also completes a CFONT whose own span list filled the whole
segment and left it with an empty payload: record 2400 segment 2 is
`cfont 3 9 2 13 4 V …` with `payload=[455,455)`, and its text is segment 3.

### 6. `c.cc`-only body lines (2 topics, and part of the unowned-cell class)

Record 261 line 3 is exactly `c.cc 15`. `prose_topic_lines.cpp` already
proved such a line draws nothing (hosted prints neither SC33-033 PREFACE.1's
`c.cc 4` nor DREICMST 1.5.6.3's bare `c.cp`); the trap family now applies the
same display-line rule.

### 7. Unrowed display lines (footnotes)

Record 177 line 40 is `    (*) IBM is a trademark of the IBM Corporation.`
inside `SRFTNFTNUNIQ16` … `SREFTN`. LayoutIR builds no physical row for it,
so its printable cells looked like an ownership gap. Hosted 2.0 serves it
under `<a name="MSG"><a name="FTNFTNUNIQ16">`, and record 177 line 38 shows
the bare `SRMSG` (a one-byte line, no operand, no payload) that hosted
renders as the empty `<a name="MSG">`. Unowned printable cells are now
admitted when the *display line* they sit on carries no physical row at all,
and a bare `SRMSG` is recorded as an anchor.

### 8. The `{0x0001, ','}` boundary residual

Record 258 line 5 ends `… DURING THE CCW SCAN,` and `cfont … 67 5 2`
highlights `SCAN,`, but the separator token 81 (`{0x0001, ','}`) is the split
the flattened decoder fired on, so the segment payload ends one character
short. The span is clipped to what the payload retained rather than
disproving the chain. This is the residual already recorded in
`display_lines.cpp` (`demote_display_line_owned_controls`) and legacy loses
it too.

## Deliberately not fixed, with the measurement

**Multi-paragraph fields run together.** One trap entry lowers to one
`ListItemIR`, whose content is an inline sequence, so a field body that the
source breaks with an empty display line loses that break. Hosted N2AH1MST
22.0 serves IAR009I's two `°` actions as two paragraphs; typed emits
`… using the BUFF option Decrease the need …`. Rejecting such an entry was
implemented and measured: it also costs **SC31-711 4.1.2, 4.2.2 and 4.3.4**,
which are typed on `main`, so the join is the family's existing behaviour and
not a new regression class. The break is detected and recorded on
`TrapTextIR::paragraph_break` (and compared in the canonical verification)
rather than enforced. Fixing it properly needs an entry model whose fields
can hold blocks.

`tests/message_prose_rows.cpp` asserted the *rendered* absence of that join
for 22.0; since the topic has moved route, that assertion was restated as
"both bullet items survive" and the row-level assertions (neither item is a
soft wrap) were left untouched.

## Result

`bootrace --coverage`, whole corpus: **6,843 → 6,868 / 7,362 (93.0% →
93.3%)**, 25 topics moved, none lost, no family reassignments.

| Book | before | after |
| --- | ---: | ---: |
| N2AH1MST | 19 | 34 |
| SC34-425 | 211 | 221 |

Whole-corpus `boo2git --force` differential against a separately built
`origin/main`: **25 files, exactly the 25 moved topics**; zero already-typed
topics changed.

Hosted comparison for all 25 moved topics (N2AH1MST DT `19910329000100`,
SC34-425 DT `19921112160049`), by hosted words the render loses:
**better 23, equal 1, worse 1**. The single lower score is SC34-425 2.4.26,
where typed drops the colon of `expected as input for FLMTCPP:` — one
character, the `{0x0001, ':'}` boundary residual above. Residual losses on the
better topics are the same in both routes: `°` bullet glyphs, `(*)` footnote
markers and the page footer.

## Remaining `SRMSG` rejections (30)

| Reason | Topics |
| --- | ---: |
| header contains `SRTBL…` — a table inside the catalog header | 5 |
| introduction rejected: record-prefix continuation has no positioned provenance (`unowned source cell`) | 4 |
| entry contains `SRTBL…` — a table inside one message entry | 3 |
| `trap entry text precedes its headline` | 3 |
| `trap entry headline is not fully highlighted` | 3 |
| `trap entry highlighted run is neither headline nor label` | 2 |
| title row is only the `Appendix <x>` prefix of the contents title | 2 |
| `trap payload has an unowned cell` | 2 |
| header contains `cz` | 1 |
| introduction rejected: `cbacklevel` (SC31-711 4.3.5, already recorded) | 1 |
| `trap entry starts with a field label` | 1 |
| `trap catalog metadata/title envelope is incomplete` | 1 |
| `trap catalog has no label repeated by every entry` | 1 |
| prose-family declines counted elsewhere (topic never reached the trap family) | 1 |

The largest of these, tables inside a message catalog (8 topics), needs the
table family inside the entry model — the same block-children gap the
paragraph break needs.
