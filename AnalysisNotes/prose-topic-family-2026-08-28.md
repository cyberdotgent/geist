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
