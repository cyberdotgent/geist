# Front matter and envelope variants in the typed prose family (issue #58) — 2026-08-29

Workflow and evidence for the slice that admitted the FRONT MATTER and
metadata-envelope variants the prose family
(`libgeist/src/prose_topic_*.cpp`) previously refused.  The normative
byte-level facts are in `Format/markup.md`, sections "Front-matter heading
forms", "Anchor controls in and around the metadata envelope" and
"`c.cp` and `c.sp` pagination and spacing controls"; this note keeps the
procedure and the hosted trail.

## Rejection buckets attacked

Measured with `build/bootrace <book> --coverage` over all 34 fixtures on
main `918eee9` (4,505 / 7,362 typed); the counts were stable across the
four main revisions this slice was rebased on:

| Reason | Topics |
| --- | --- |
| `topic metadata controls are incomplete or out of order` | 272 |
| `heading level '<form>' is not an h1-h6 prose heading` | 223 |
| `c.cp control carries visible payload '<x>'` | 201 |
| `c.cp control has no operand` | 41 |
| `body control c.sp is outside the prose model` (surfaced once the above cleared) | 133 |
| `control SR<id> carries visible payload '<x>'` (body anchors) | 23 |

## Procedure

1. Per-topic census: `bootrace <book> --coverage` for all 34 books, then the
   rejection strings normalised and grouped.  Each bucket was then
   dissected topic by topic with `bootrace <book> <topic> --segments` and
   `--ir` (physical rows plus the ownership ledger).
2. Token-level evidence where the decoded projection is ambiguous: the
   ownership dump was read back into a per-token table
   (record, token, encoded width/value, decoded words, Layout IR
   disposition) so that "operand" versus "display text" could be decided on
   token adjacency instead of on the number of spaces in the flattened
   string.  This is what settled `c.cp`.
3. Every reading was checked against hosted BookServer
   (`bookmgr.exe/BOOKS/<book>/<topic>?DT=<dt>`, latin-1).
4. Whole-corpus `boo2git --force` before/after, and a hosted word-level
   comparison of every moved topic whose book is in the hosted catalog.

## What the buckets are

### `topic metadata controls are incomplete or out of order` — one cause

All 272 topics (SC33-033 127, SC24-546 111, SC34-425 34) carry exactly one
extra segment, an `SRLEN` structural control between `csummary` and
`chdlevel`.  Hosted serves it as a plain anchor immediately before the topic
heading whose **name is the whole control without `SR`**, so a payload
extends the name rather than becoming body text:

- `SC24-546` 3.1 record 161 segment 6, complete `[90,103)` = `SRLEN ADDRESS`
  → `<a name="LEN ADDRESS"><a name="HDRADDRESS"><H2> 3.1   ADDRESS</H2></a></a>`
  (DT `19940323131240`).
- `SC24-546` 4.3.6 (`SRLEN`, no payload) → `<a name="LEN">`.
- `SC33-033` 4.6 → `<a name="LEN CHAATT">` (DT `19930422134757`).
- `SC34-425` 2.4.3 → `<a name="LEN FLMCSPDB DB2 Bind/Free Translator">`
  (DT `19921112160049`).

The model therefore admits **any** non-reserved bare `SR<id>` structural
control among the metadata controls, as a leading anchor whose id is the
trimmed complete output minus `SR`.  All of its tokens are envelope
metadata; none is visible.

A body `SR<id>` anchor is the *opposite* case and needed its own rule: its
id is the opcode without `SR`, and its payload is the first display line of
the text it names, which hosted wraps in the anchor element
(`ACPZMST1` record 155 `SRSPTSETDC A domain controller handles ...` →
`<a name="SPTSETDC">   A domain controller handles ...</a>`).  Those
payload tokens re-enter the display-row stream.

### `heading level '<form>' is not an h1-h6 prose heading` — front matter

Twelve named `CHDLEVEL` forms, all of them book front/back matter, all
served as `<H1>`: `toc` 34, `vnotice` 32, `index` 29, `cover` 29,
`preface` 27, `notices` 21, `glossary` 17, `title` 9, `soa` 9, `bibliog` 8,
`abstract` 5, `abbrev` 3.  One topic per form was fetched from hosted and
its `<H1>` recorded (table in `Format/markup.md`).  The raw form is kept on
`ProseTopicIR::heading_form` as provenance rather than being coerced
silently; any other form still fails closed.

### `c.cp control carries visible payload` — a mis-read operand

`c.cp` is the keep-together pagination control.  The tracker recorded it as
pagination-only, and the family required its operand to be all digits and
everything after it to be spacing.  Both halves were wrong:

- The **operand** can carry a unit suffix.  Corpus wide: bare counts
  `4`..`999`, plus `1i`, `2i`, `50p`, `8DV`.  Hosted prints none of them
  (`GC23-046` `CHANGES.1` has no `DV` anywhere, DT `19920330095121`;
  `DREICMST` `2.20.3.1.4` has no `2i`, DT `19911219125856`).
- The **payload after the operand is genuine display text** that the legacy
  renderer silently drops unless it is spelled `<n>:<text>`.  Hosted serves
  it: `FA1PLMM0` 6.4.1 `   The columns have the following meaning:`
  (DT `19910927114801`), `GC28-183` 6.4 `   6.  SYSOUT data sets (except
  DD3 and DD4) are printed on the form called` (DT `19930625102617`),
  `IEAC6MST` 2.1 ` |     If you do not already have a dump directory, ...`
  (DT `19920124000100`).

The discriminator is positional, from the Token IR: **the record encoder
emits no spacing token between a control opcode and its operand**, so the
operand is the token adjacent to `c.cp`; a spacing token in between proves
there is none.  Byte-level examples:

```
IEAC6MST rec 79 : 'c.cp' '999' '      ' ' ' '|' '    ' 'If'      -> operand 999
GC28-183 rec 783: 'c.cp' '              ' '   ' '6' '.' 'SYSOUT' -> no operand
FA1PLMM0 rec 369: 'c.cp' ' ' '   ' 'The' 'columns'               -> no operand
DREICMST rec 243: 'c.cp'                                          -> no operand
```

The flattened decoded string cannot decide this: `c.cp 6.` and `c.cp 999`
differ only in the width of a space run the projection collapses.

### `body control c.sp` — vertical space

Only three operand spellings exist corpus wide (scanned over every segment
of every topic of every book): `<n> c` (160), `<n> p` (16, all inside
generated `INDEX` bodies, which never reach the prose family) and
`<n>p p c` (5).  None is displayed; hosted emits only the paragraph break
(`GC28-183` 1.3.3, `SC33-033` 4.6, `SC34-425` 2.4.3).  `<n> p` is left
fail-closed because no hosted page was found that exercises it.

## A shared-path defect the slice had to fix

Admitting front matter exposed the residual risk recorded in
`AnalysisNotes/prose-topic-family-2026-08-28.md`: the rule that a glued
one-byte alphabetic token below the row-control byte limit is a row-control
slot fired **inside** a hyphenated compound.  `SG24-204` `PREFACE` record 17
spells `step-by-step` as the glued run `step` `-` `by` `-` `step`, where
`by` has encoded value `0x2f`; hosted (DT `19971218054640`) serves
`a step-by-step manner`, so the slot reading dropped `by`.

The rule now also requires that the candidate does **not** continue a glued
compound (no following visible token attached with prefix 0/1, directly or
through an attach-prefix bare token).  The N2AH1MST evidence the rule was
built on is unaffected: there the slot (`access`, `0x1c`) is followed by the
row's origin space run, not by glued text.

Seven already-typed topics gained a word back, each confirmed against
hosted:

| Topic | Restored | Hosted |
| --- | --- | --- |
| `FA1PLMM0` I.6.1 | `link-and-go` | DT `19910927114801` |
| `GG24-4302-00` 2.1.1 | `(a) recognizes` | DT `19950308184737` |
| `ITPPIBOK` 7.6.3.2 | `send-and-receive` | DT `19910628074854` |
| `QSYSINFO` 1.1.6, 2.1.72, 2.2.85 | `question-and-answer` | DT `19910524120827` |
| `SC34-425` 2.11.11 | `If Step (a) is successful` | DT `19921112160049` |

## Measured

Baseline: main `918eee9`, built from `git archive main libgeist` into a
scratch build tree.

- **Typed-route coverage 4,505 → 4,817 of 7,362 (61.2 → 65.4%, +312)**;
  312 topics moved legacy → typed, **none moved the other way**, and no
  book regressed.
- Largest per-book gains: SC24-546 +42, OFCUSEOV +30, FA1PLMM0 +27,
  SH12-565 +27, SC34-425 +26, SC09-138 +22, IEAC6MST +21,
  SC24-5520-00 +18, DREICMST +10, GC28-183 +10, ACPZMST1 +9,
  SC33-033 +9, GG24-395 +7.
- Whole-corpus `boo2git --force` before/after: **319 files changed, 0
  added, 0 removed** — the 312 moved topics plus the seven already-typed
  topics of the compound-word fix above.
- Hosted word-level comparison of **every** moved topic whose book is in
  the hosted catalog: **278 topics across 26 books**, typed **better on 227,
  equal on 49, worse on none**.

### Difference classes decided

| Class | Typed behaviour | Decision |
| --- | --- | --- |
| Front-matter heading | `# <id> <title>` at level 1 | keep: hosted serves `<H1>` for every form |
| Envelope anchor | `<a id="LEN ADDRESS">` before the heading | keep: the served spelling |
| Body anchor payload | anchor block then the display text | keep: hosted wraps the same words in the anchor |
| `c.cp` display payload | emitted as body text | keep: hosted prints it, legacy dropped it |
| `c.cp`/`c.sp` operands | never emitted | keep: hosted prints none of them |
| Hosted page footer (`© Copyright IBM Corp <years>`, `IMPORTANT: read the special notices section`) | not emitted | accept: hosted-only page chrome, as for every other family |
| Hosted `COVER`/`TITLE` metadata box (Title / Document Number / Build Date) and toolbar images | not emitted | accept: hosted-only UI, recorded by the earlier audit |
| Markdown escaping (`\(February 1995\)`, `U\.S\.A\.`) | escaped | keep: the typed renderer convention; four tests updated |
| `__` checklist rows | literal `\_\_ lnmstatus` | keep: hosted prints `__     lnmstatus` (SC31-711 2.4.9); legacy's `- lnmstatus` was wrong |
| Multi-line fixed table rows (`SC33-033` 4.132) | one logical row `\| 703 \| CHSTRT \| ... \|` | keep: hosted's `<pre>` splits the code onto a second physical line, so a sequence diff reports a word-order change while the word multiset is identical except hosted's footer; legacy loses 15 content words |
| Implicit multi-column word lists (`SC09-138` 6.2.7) | read column-major | accept: no word lost (multiset identical except hosted's footer); legacy loses four content words |
| A rule-less `Term / Trademark of` grid the fixed-table block declines (`SC31-711` FRONT_1.1) | reflowed into one paragraph | **the one structural regression**: word for word equal to hosted and to legacy, but legacy's Markdown table was closer to hosted's aligned `<pre>` columns; the implicit-grid guard only fires on a plural `CFONT` header, recorded as a follow-up |

Both "worse" sequence-ratio verdicts above were re-checked as word
multisets: typed loses only hosted page chrome, legacy loses real content.

## Hosted DTs used

Those already tabulated in `AnalysisNotes/prose-topic-family-2026-08-28.md`
plus, from `AnalysisNotes/bookserver-dataset-2026-08-25.md`:

| Book | DT |
| --- | --- |
| SC24-546 | `19940323131240` |
| SC33-033 | `19930422134757` |
| SC34-425 | `19921112160049` |
| GC28-183 | `19930625102617` |
| SC26-457 | `19911220191142` |
| SC31-605 | `19911015203151` |
| SH12-565 | `19941206115523` |
| PRG1SORT | `19900829171904` |
| SG24-204 | `19971218054640` |
| IBMMMSTR | `19911004151140` |
| SC41-485 | `19951003131222` |
| XWEBDEMO | `19970423182524` |

`SC24-5520-00`, `SC24-5527-02`, `SC28-1881-05`, `SC09-2417-00`,
`GX27-3999-00` and `packet` remain absent from the hosted catalog and are
excluded from the hosted verdict.

## Remaining rejections of the 2,545 legacy topics

Top classes after this slice: visible token inside a table region claimed by
no block 297, placeholder run followed by visible text 256, selector targets
a picture or external link 224, span starts/ends inside a word 232,
font/selector span exceeds the display line 120, trailing menu label
mismatches 181, body control glued into prose text 100, `text segment begins
with control-like word` 95 (of which `ctocdef=0` and `cidelm` are the
generated TOC/index streams of `CONTENTS` and `INDEX`), placeholder glyph
inside prose text 87, `ST title does not match the topic title` 77, `first
record lacks the topic metadata envelope` 69, `body control SRMSG is outside
the prose model` 49.

Front matter specifically: `COVER` still fails closed on the placeholder-run
class, and `CONTENTS`/`INDEX` because their bodies are generated TOC/index
streams (`ctocdef`, `ctoce`, `cidelm`, `citerm`) that belong to the
generated-list family, not to prose.
