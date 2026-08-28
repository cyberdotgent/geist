# Typed prose topic family (issue #58) — 2026-08-28

Workflow and evidence for the ordinary-prose lowering
(`libgeist/src/prose_topic_*.cpp`), the single largest typed-route slice.
Format facts derived here are in `Format/markup.md`, section "Flattened
prose display rows"; this note keeps the procedure and the hosted trail.

## Procedure

1. Corpus census: a scratch program (`try_lower_topic_to_document_ir` per TOC
   topic, per book) listing route, rejection reason, control kinds and
   marker slots; iterate on the largest rejection class each round.  Rounds:
   71 (baseline) → 1,537 → 2,420 → 2,622 → 2,637 typed of 7,308 topics
   (all books except N2AH1MST).
2. Row grammar derived from token dumps (`bootrace --ir`, a scratch token
   tracer printing encoded width/value, decoded words and ownership) and
   checked line by line against hosted `<pre width="80">` pages, which show
   the display rows and one `<p>` per paragraph.
3. Hosted comparison harness: typed Markdown → words (Markdown syntax and
   escapes stripped) versus hosted body words versus the legacy renderer
   built from the base commit (`git archive HEAD libgeist`, built in
   scratch).  `DT` values come from each book's hosted `CCONTENTS` page.
4. `bootrace --coverage` per book for the final numbers; `boo2git --force`
   before/after over all books except N2AH1MST, whose topics were rendered
   one by one with `boorender --md` under `timeout 120`.

## Hosted DTs used

| Book | DT |
| --- | --- |
| SC31-711 | 19941010174546 |
| QSYSINFO | 19910524120827 |
| QSYSNEWG | 19910524085706 |
| SC24-5520-00 | 19920529132045 (hosted edition differs textually from the fixture) |
| SC09-138 | 19920918183032 (most topics not served) |
| FA1PLMM0 | 19910927114801 |
| SH20-918 | 19910520154851 |
| ACPZMST1 | 19920319123146 |
| GG24-4302-00 | 19950308184737 |
| DREICMST | 19911219125856 |
| GC23-046 | 19930208105051 (hosted edition differs) |
| ITPPIBOK | 19910628074854 |
| IEAC6MST | 19920124000100 (discovered from its hosted `CCONTENTS`) |
| SC28-1881-05 | 19920313000100 |
| SC24-5527-02 | 19920529132045 (hosted serves different topic bodies under these ids) |

## Difference classes against hosted and legacy

| Class | Typed behaviour | Decision |
| --- | --- | --- |
| Markdown escaping (`2\.2\.1`, `FRONT\_1`, `\_\_`) | renderer escapes punctuation in identity and text | keep: the typed renderer convention used by every family; tests updated |
| Link spelling `(<#id>)` | anchor destinations in angle brackets | keep: same form as typed menus; `boo2git` rewrites both |
| Emphasis | adjacent same-style CFONT words merge into one span (`*Getting Started with …*`); C→emphasis, X/E/4→code, P→code, V→emphasis, R/H-M→strong, L→emphasis | keep: hosted `<cite>`/`<tt>`/`<samp>`/`<kbd>`/`<var>`/`<B>`/`<I>` |
| Bullets | `◆` rows become `- ` items; `__` checklist rows stay literal paragraphs (hosted prints `__`) | keep |
| Hosted-only glyphs (`|` row marks, `*`/`**` trademark marks that are unmapped) | dropped, as the legacy route drops them | accept |
| Two-column forms, tables, figures, `CZ` dialect | fail closed (legacy keeps them) | by design |

## Residual risks

- One-byte words with encoded value below 48 before a lone origin run are
  treated as row-control slots (SC31-711 `a`/`action`/`any`/`application`/
  `access`, ACPZMST1 `a`); a genuine low-value word in that shape would be
  dropped.  Values 48 and above are kept as text (QSYSNEWG `400`, `IBM`).
- Nested bullet lists flatten into the parent item's text (word-equal to
  hosted; the DocumentIR list item has no child blocks).

## Composition: table and figure spans (2026-08-28, later run)

The prose family previously failed a whole topic closed whenever it saw an
`SRTBL` envelope or an `SRFIG` figure.  The body is now a sequence of spans
in source order: prose spans (the display-row model above) interleaved with
table spans (`extract_fixed_table_blocks_ir` →
`lower_fixed_table_block_to_document_ir`) and figure spans
(`extract_figure_blocks_ir` → `lower_figure_block_to_document_blocks`).

Procedure and conservation rule:

1. `plan_spans` (`libgeist/src/prose_topic_spans.cpp`) runs *before* the body
   token stream is built.  It extracts every table envelope and every figure
   region of the topic and fails the topic closed if any one of them is
   declined by its extractor, so there are no partially typed topics.
2. Each admitted region becomes a source extent (first control token through
   last owned token).  Every token the block claims takes the span's ledger
   role (`table` / `figure`) and names the span; a token inside the extent
   that the block does not claim is admitted only when it carries no visible
   word (bare spacing, space run, decoder placeholder run, separator, or a
   one-byte hidden marker slot) and becomes region structure of the same
   span.  Any other token inside a region rejects the topic.
3. The stream pass then skips every segment inside a region, so prose text
   and span text can never claim the same token; double claims are refused by
   the ledger.  Prose glued after `SRETBL`/`SREFIG` in the closing segment
   re-enters the stream as body text.
4. A picture-less `SRFIG<id>` frame around table spans contributes only its
   anchor: hosted BookServer serves `<a name="FIGTBLUNIQ6">` immediately
   before `<a name="TBLTBLUNIQ6">` on SC31-711 4.0.  Recorded in
   `Format/markup.md` under `SRTBL<id>`.
5. Verification: the topic verifier re-extracts the spans canonically and
   compares them cell for cell, checks that every span position is in source
   order, that every span-role token names its own span, and that no prose
   block claims a token inside a span; the DocumentIR verifier re-lowers.
   `libgeist/tests/prose_topic_spans_synthetic.cpp` adds mutation tests
   (dropped span, dropped table block, span moved to another position,
   figure caption edited, table cell edited, prose text claiming a span
   token, prose paragraph inserted at a span position).

### Hosted DT corrections

The DTs used in the first prose run were wrong for two books and unavailable
for several others; the authoritative list is the live catalog
(`bookmgr.exe/FINDBOOK?filter=&SUBMIT=Find`, 11,930 `BOOKS/<id>/CCONTENTS?DT=`
entries) cross-checked with `AnalysisNotes/bookserver-dataset-2026-08-25.md`:

| Book | DT | Note |
| --- | --- | --- |
| GC23-046 | `19920330095121` | `19930208105051` is the `-02` edition and answers with a cover page |
| SC09-138 | `19910321130500` | earlier value was a different edition |
| SC24-5520-00 | — | "could not be located in the BookServer catalog" |
| SC24-5527-02, SC28-1881-05, SC09-2417-00, GX27-3999-00, packet | — | same; excluded from hosted sampling |
| IEAC6MST | `19920124000100` | |
| OFCUSEOV | `19900805103816` | |
| N2AH1MST | `19910329000100` | |

### Difference classes of the composed route

| Class | Typed behaviour | Decision |
| --- | --- | --- |
| Figure image alt text | `![Figure 33\. …](<resource:16>)` repeats the caption; legacy wrote `![Resource 16]` | keep: the alt text is the caption hosted prints, and the legacy alt was wrong |
| Figure anchor id | `FIG<id>` (hosted `<a name="FIGFDCE107">`); legacy truncated it to `FDCE107` | keep: matches hosted |
| Table cell word boundaries | hosted splits `INFILE(` from `ddname` because `<kbd>`/`<var>` markup sits between them; the typed cell is one word | accept: markup-only boundary, no text lost |
| Multi-line table cells | joined with `<br>`; legacy split them into extra columns and truncated cells (SC26-457 3.24 `INDATASET(entryna`) | keep: typed reproduces hosted |
| Angle-bracketed literals (`\<stdio\.h\>`) | kept | keep: hosted prints them; only the word-diff harness has to ignore them |
| A `c.<xx>` opcode glued to a text run by the decoder | rejects the topic | fail closed: hosted serves no such word (SH20-918 3.31.1, DREICMST 1.7.7.3, SH12-565 1.1.2, GG24-4302-00 8.1.5), and the previous behaviour printed it |

## Composition: table and figure spans (2026-08-29)

The prose body is now a sequence of spans in source order: prose spans
interleaved with table spans (one admitted `SRTBL ... SRETBL` envelope each,
`extract_fixed_table_blocks_ir` -> `lower_fixed_table_block_to_document_ir`)
and figure spans (one admitted region each, `extract_figure_blocks_ir` ->
`lower_figure_block_to_document_blocks`).  Implementation:
`libgeist/src/prose_topic_spans.cpp` plus the span hooks in
`prose_topic_stream/lines/blocks/document_lowering`.

### Conservation model

1. `plan_spans` runs before the body stream pass.  It extracts every table
   envelope and figure region of the topic and fails the topic closed on any
   decline, so there are no partially typed topics.
2. Each region is the source extent from its opening control's first token
   to its last owned token.  Every token of every block is claimed with the
   span's ledger role (`table` / `figure`) and names its span; an unclaimed
   token inside a region is admitted only when it carries no visible word
   (bare spacing, space run, decoder placeholder, one-byte marker slot),
   anything else rejects the topic.  A block claim that falls outside its
   region is admitted only for such an invisible token (the spacing token
   before `SRFIG`, ACPZMST1 1.1.3) and stays with the stream.
3. The stream pass skips the region's segments, emitting one span item at
   its first segment; tokens of the closing segment that the region does not
   own (`SREFIG?  The routers ...`) re-enter the stream as body text.
4. A span is a hard row boundary in the display-line pass and splits the
   block before it; spans and anchors at one position keep their source
   order (`ProseSpanIR::anchors_before`).
5. `verify_prose_topic_ir` re-extracts canonically, proves every span token
   and span/block reference, and runs `verify_fixed_table_blocks_ir` /
   `verify_figure_blocks_ir` over the composed block sets; the DocumentIR
   verifier runs over the composed document.

### Anchors and links proven against hosted

- A picture-less `SRFIG<id>` wrapping a table is a frame: it contributes
  only its anchor (SC31-711 4.0 serves `<a name="FIGTBLUNIQ6">` then
  `<a name="TBLTBLUNIQ6">`, DT 19941010174546).
- The table anchor is the whole `SRTBL` opcode without `SR` (`TBL<id>`);
  cross references select that spelling (GG24-4302-00 10.2 `TBLDBCTL51`).
- A `CSELECT` inside a table span whose column range meets a cell and whose
  payload covers a whole cell line lowers that line to a cross reference
  (SC31-711 4.0 "LNM OS/2 Agent Application Traps" in / topic 4.1).

### Measured

Baseline: main `8c3372e` (gap-column tables admitted, ownership-disposition
conflicts fixed), built from a `git archive` of that commit into a scratch
build tree.  `bootrace --coverage` over every book, N2AH1MST included (it
now finishes in the same budget as the rest):

- **2,665 -> 3,364 of 7,362 typed topics (36.2% -> 45.7%, +699)**.
- 708 topics moved legacy -> typed; 9 moved typed -> legacy because they now
  fail closed rather than print a `c.<xx>` control opcode the decoder glued
  into a text run (hosted serves no such word: DREICMST 1.7.7.3 and B.1,
  SC33-033 B.3, SH12-565 5.1.7.1/APPENDIX1.2.5.1/APPENDIX1.4.8.3.2/
  APPENDIX1.5.4/BIBLIOGRAPHY.2, SH20-918 3.31.1).
- Gains by pre-move structural class: mixed 517, fixed rows/tables 97,
  figures/images 91, selectors 3.  By signature: selectors+figures 197,
  tables 97, tables+selectors 93, figures 91, tables+figures 59,
  tables+figures+menu 27, selectors+figures+menu 25,
  tables+selectors+figures 23, figures+menu 23, lists+selectors+figures 21,
  tables+menu 21, lists+figures 17, and 13 more.
- Every book grows or stays level; largest: SC24-5520-00 +152, SC26-457 +66,
  FA1PLMM0 +64, SC31-605 +62, SC09-138 +47, DREICMST +46.

Whole-corpus `boo2git --force` before/after (N2AH1MST excluded from the
export as before): **715 changed files, 0 added, 0 removed**; 706 are topics
that moved to the typed route and the other 9 are the fail-closed
regressions listed above.  No file changed for any topic that stayed on the
same route.

Hosted word-level sample of **86 moved topics across 23 books** (29 of them
carrying both a table and a figure), each of them a topic whose exported
Markdown actually changed: typed **better on 76, equal on 10, worse on
none**.  The comparison uses two tokenizations - the literal one and a
markup-independent one (alphanumeric runs, image alt text and `<br>`
dropped) - because hosted `<kbd>`/`<var>` markup splits words such as
`INFILE(` + `ddname` that a typed table cell keeps glued.

Hosted DTs were re-derived from the live catalog
(`bookmgr.exe/FINDBOOK?filter=&SUBMIT=Find`, 11,930 `CCONTENTS?DT=` entries)
against `AnalysisNotes/bookserver-dataset-2026-08-25.md`.  Corrections:
GC23-046 is `19920330095121` (the `-02` DT answers with a cover page) and
SC09-138 is `19910321130500`.  SC24-5520-00, SC24-5527-02, SC28-1881-05,
SC09-2417-00, GX27-3999-00 and packet are absent from the hosted catalog
("could not be located") and are excluded from hosted sampling.

| Difference class | Typed behaviour | Decision |
| --- | --- | --- |
| Drawn/picture figure captions and box bodies hosted shows and legacy drops | emitted from the typed figure block | keep (most of the 76 "better" rows) |
| Table cells legacy split into extra columns and truncated (SC26-457 3.24 `INDATASET(entryna`) | one cell with `<br>` line breaks, as hosted's `<pre>` rows | keep |
| Figure anchor id | `FIG<id>`, the served spelling; legacy truncated it (`FDCE107` for hosted `FIGFDCE107`) | keep |
| Image alt text | the caption text; not displayed by any renderer | keep; the harness ignores alt text |
| Word boundaries inside a table cell | hosted splits `INFILE(` from `ddname` on `<kbd>`/`<var>` markup | accept: markup-only boundary, no text lost |
| `&amp;`, `\<stdio\.h\>` | kept as written | keep; only the harness has to unescape them |
| Hosted-only glyphs (`°` bullets, `©`) | dropped, as the legacy route drops them | accept |
| A `PIC<n>`/`LNK` selector inside a table cell | rejects the topic | fail closed: hosted serves the cell as an `<img>` (GG24-395 3.3.8) and the table block has no picture cell |
| A `c.<xx>` opcode glued to a text run | rejects the topic | fail closed: hosted serves no such word |
| Hosted edition differs / book absent from the catalog | not comparable | excluded from the verdict |
