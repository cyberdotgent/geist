# Glued body controls and topic titles (issue #58)

The control/title cluster of the prose family: four rejection reasons that all
turned out to be decisions taken on the **flattened decoded string** where the
evidence only exists at **token** level. Format facts live in
`Format/logical-controls.md` ("Body Controls Without A Boundary Byte", "A Word
Is A Control Only Where A Boundary Byte Precedes It", "The Metadata Envelope
Spans Records") and `Format/markup.md` ("The ST Title Is One Display Row").
This note records the workflow, the hosted sources and what stays fail-closed.

## Tooling

`bootrace <book> <topic> --tokens` was added for this slice. It dumps the
record's `LogicalRecordIR` token by token — ordinal, encoded value and width,
spacing prefix, payload byte range and the decoded words. Every finding below
was read off it; none of them is visible in `--segments` or `--records`,
because the decoded projection renders the attach control, every box-drawing
word and every unmapped word all as `?`.

## Class 1 — "body control X is glued into prose text" (107 topics)

`c.cp` (keep together), `c.cc` (conditional column) and `c.pa` (page eject) are
SCRIPT controls stored as one **two-byte** dictionary token. The encoder does
not always write a boundary byte before them, so the string-level splitter —
which requires a space, `=`, `,` or `.` *after* the opcode — never saw them at
all, and the composition slice had fail-closed on the spelling.

Byte-level evidence (`--tokens`):

* FA1PLMM0 record 353: `71 VSE`, `72` encoded value 2 width 1 words
  `{0x0001, ','}`, `73 c.cp` value 49655 width 2, `74` attach, `75` rule run.
* GG24-4302-00 record 613: `17 SREFIG`, `18` bullet glyph, `19 c.cc` value
  52750, `20 20`.
* SC09-138 record 1482: `45 other`, `46` `{0x0001,'.'}`, `47` `{0x0001,','}`,
  `48 c.pa`.
* SC33-033 record 241: `53 ,`, `54 SREFIG`, `55` junction glyph, `56 c.cc`
  value 53126, `57 2i`.

Hosted (`FA1PLMM0` DT 19910927114801, `GG24-430` DT 19950308184737,
`SC09-138` DT 19910321130500) prints neither the opcode nor the comma:
`   °   PSF/VSE`, the figure with its caption and no `20`, and `each other.`.

`decode_control_segments` now splits the decoded span at the opcode's own
source token and takes a directly preceding compact separator token into the
new segment; the stream's existing `c.cp` handler was generalised to the three
opcodes. The reverse mistake is guarded: a **one-byte** token below the
row-control limit (48) that merely spells `c.<xx>` is the next display line's
length byte — IBMMMSTR record 1244 token 139, encoded value 31, spells `c.cc`
and stands between the `:` closing `Messages print at run-time when:` and the
origin run of `1.  An error occurs ...`.

## Class 2 — "text segment begins with control-like word X" (99 topics)

The same problem seen from the other side, and the same shape as the `ee31d26`
`SH<id>` precedent: the splitter opens a segment wherever a word is *spelled*
like a control. Corroboration is the boundary token, not the spelling.

Controls carry one: ACPZMST1 CONTENTS record 18 writes `ST` after token 24
(one word U+2514) and `ctocdef=0` after token 30 (U+2518); SC24-546 record 961
writes `SRHDRIRRR` after the compact `,` that closes `csourcefn DMSB1IRR`;
QSYSINFO GLOSSARY record 756 writes `SRGLS AFP` after token 189 (U+2666).

Prose words do not: PRG1SORT 1.1.5.1 record 80 token 2 `SRCFILE` follows an
18-cell space run and is the CL parameter `SRCFILE(LIBRAR2/FILE3)`, which
hosted DT 19900829171904 serves as `<tt>SRCFILE(LIBRAR2/FILE3)</tt>`.

A text segment with no boundary before it is now prose continued. The `SI`
index keyword keeps its own reading — making it a continuation printed hidden
index lines as body text, which is exactly the defect the table-region slice
reported.

## Class 3 — "ST title does not match the topic title" (79 topics)

Three separate faults, all in the row model or in what the check demanded.

1. **The row geometry crossed the `ST` segment end.** SC24-546 E.2 record 1169
   ends the segment with `)` (token 35), and the fill/origin run after it
   belongs to the next segment, so the `)` was read as that row's marker slot:
   `The File Block (FBLOCK`. Hosted DT 19940323131240 serves
   `<H2> E.2   The File Block (FBLOCK)</H2>`. The reading is now refused while
   in the title — except for the documented compact-marker collision, where a
   one-byte token spelling a whole dictionary word really is the slot
   (FA1PLMM0 I.6.1 `access` value 43, SC24-5520-00 3.7.5.2 `and`,
   SH20-918 3.33.14 `an`); restricting the refusal to punctuation glyphs was
   found by the whole-corpus differential, which flagged exactly those three.
2. **A second title marker.** ACPZMST1 5.4/5.5 store `ST` + spacing + a
   placeholder slot + `/` + `etc`; the `/` was taken as a second slot and the
   heading lost its leading character.
3. **The catalog title is a different truncation of the same control.**
   `build_topics` reads the flattened record and stops at the first decoder
   boundary; the display row stops at the row break. QSYSINFO 2.1.21's catalog
   string is `... COBOL User's Guide and Re` while hosted heads the topic
   `... COBOL User's Guide and` and starts the body with `Reference`.
   The check now corroborates positionally: the typed title and the catalog
   string must both be prefixes of the `ST` payload's visible word run
   (`LineBuild::title_run`), which is accumulated with the same spacing rules
   as the row cells and treats a row break as a word break.

## Class 4 — "first record lacks the topic metadata envelope" (69 topics)

Not a defect in the topic at all: the metadata run simply continues in the next
logical record. Break points observed, one per required control — GC28-183
2.3.5 after `CTOPICN` (records 163/164), SC41-485 COMMENTS after `CBACKLEVEL`
(455/456), QSYSINFO 2.1.57 after `CSUMMARY` (163/164), SC34-425 1.5.5 after
`CHDLEVEL` (241/242), ACPZMST1 5.7 after `CSOURCEFN` (289/290).
`parse_envelope` walks the topic's segments in source order.

## Measurement

Baseline is `main` `51a686e`, measured with a build of it.

* Coverage `bootrace --coverage` over all 34 fixtures: **5,720 -> 6,072 /
  7,362 (82.5 %)**, 352 topics moved legacy -> typed, none the other way.
  Per book: SC33-033 +105, QSYSINFO +56, OFCUSEOV +33, SH12-565 +27,
  SC31-605 +18, SC24-5520-00 +12, SC24-546 +11, SC09-138 +10, DREICMST +9,
  SC28-1881-05 +9, PRG1SORT +7, SH20-918 +6, SC09-2417-00 +5, SC26-457 +5,
  ACPZMST1 +5, IEAC6MST +4, QSYSNEWG +4, SC24-5527-02 +4, GC28-183 +4,
  FA1PLMM0 +4, ITPPIBOK +3, GG24-4302-00 +3, SC34-425 +3, packet +2,
  SC31-711 +1, GG24-395 +1, SG24-204 +1. No book regressed.
* Reason counts on the same two builds: glued body control **107 -> 1**;
  first record lacks the envelope **69 -> 3**; ST title mismatch **79 -> 0**;
  control-like word **99 -> 73** (the remainder is the generated TOC/INDEX
  family below). All four residuals are listed under "Still fail-closed".
* Whole-corpus `boo2git --force`: **352 changed files, 0 added, 0 removed** --
  exactly the 352 moved topics, with no already-typed topic changed (checked
  in both directions).
* Hosted, character-level against the served `<pre>`: 63 of the moved topics
  across 24 books were servable (`ACPZMST1 DREICMST FA1PLMM0 GC23-046
  GC28-183 GG24-395 GG24-4302-00 IEAC6MST ITPPIBOK OFCUSEOV PRG1SORT QSYSINFO
  QSYSNEWG SC09-138 SC09-2417-00 SC24-546 SC26-457 SC31-605 SC31-711 SC33-033
  SC34-425 SG24-204 SH12-565 SH20-918`; SC24-5520-00, SC24-5527-02,
  SC28-1881-05 and packet are absent from the hosted catalog).
  **Typed better on 43, equal on 20, worse on 0.** Difference classes, all
  decided:
  * bullet glyph `°` rendered as a Markdown list marker -- convention;
  * grid cell order -- FA1PLMM0 14.2.4's access-rights table and SC09-138
    FRONT_1.2's trademark grid keep every cell (word multiset identical to
    hosted, while legacy loses 17 and 10 words respectively) but group them
    into two columns where hosted lays out three. The character-sequence
    metric scores those two as "worse"; both are content-complete, and the
    column grouping is a follow-up for the gap-table model;
  * adjacent same-style words merged into one Markdown span
    (`<B>a</B> <B>b</B>` -> `**a b**`);
  * anchors emitted as `<a id="...">` by both routes;
  * topics the host serves from a different edition (ACPZMST1 5.4/5.7,
    ITPPIBOK 2.4.3, PRG1SORT B.0, SC34-425 1.5.5, SC24-546 E.3) -- legacy and
    typed lose identically.

## Still fail-closed

* **Generated TOC and INDEX topics** — 33 `ctocdef` (every book's CONTENTS),
  26 `cgpsep` and 3 `cidelm` (every book's INDEX), 1 `citerm`, 1 `ctoce`.
  These are real controls with real boundary tokens; the `generated_list`
  family only models `:figlist`/`:tlist`, so `:toc` and `:index` need their
  own family (`ctocdef=<n> <a> <b> <c>` level definitions plus
  `ctoce <level> <n> <topic-id> <title>` entries; the index uses `cidelm`,
  `cgpsep` and `citerm`). Roughly 64 topics.
* **`c.rev`** (IBMMMSTR PREFACE.6) — a revision-bar control; it is a one-byte
  token, so the length-byte guard keeps it closed and no hosted evidence was
  gathered.
* **SC26-457 FRONT_2.1.1, FRONT_2.1.2, FRONT_3.2** — the `ST` control is
  swallowed into the `csourcefn` segment's payload, so the envelope run has
  eight controls but no ninth segment.
* **Corrupt catalog titles** — the catalog string swallowed a
  source token the display model proved invisible: `:MSGNO` (IBMMMSTR 1.11.2,
  2.2, 3.4, 3.8), a stray `'` (IBMMMSTR 1.6, 1.12), a `<BOOK>` selector kind
  word (ITPPIBOK D.1.3.4), the row-control word `Access` (N2AH1MST 1.2.4).
  These are the same corruption the menu-label slice recorded; fixing them
  means giving the catalog a typed title, not relaxing the comparison.
* **`SR<id>` anchors that are really prose words.** `classify` resolves
  `SRRCMIT`, `SRRBACK` (SC24-546 14.0), `SRFILTER` (SC31-605 2.0) and
  `SRVPREF` (SH12-565 3.1.11, 3.1.3.3) to structural anchors on spelling
  alone, so the word disappears from the body and only the anchor remains.
  Hosted prints all of them as ordinary words. The legacy route loses the
  same words, so this is not a regression, but it is a real defect. Applying
  the boundary rule to *structural* segments was tried and reverted: the
  demotion path assigns the opcode the control role, so the word is still
  lost, and it wrongly demoted glossary `SRGLS <term>` anchors (whose payload
  extends the anchor *name*, hosted `<a name="GLS advanced function printing
  (AFP)">`), duplicating every glossary term. The fix needs the demoted
  segment to re-enter the stream with **every** token as payload.
