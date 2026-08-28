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
