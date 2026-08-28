# Typed-route census, 2026-08-28

Measurement of how many TOC topics reach the typed Document IR route
(`try_lower_topic_to_document_ir` produced a verified `DocumentIR`) versus the
legacy string pipeline, taken with the new permanent inventory API rather than
a render-time trace. Tracker: issue #58 (the 2026-08-28 correction comment).

## How it is measured

- Library: `BooDocument::typed_route_inventory()` in
  `libgeist/src/typed_route_inventory.cpp` (types in
  `libgeist/src/geist/detail/typed_route_inventory.hpp`). For every TOC topic
  with a topic header it decodes the positioned sources, extracts Layout IR,
  and calls `try_lower_topic_to_document_ir` with the new
  `TypedLoweringTraceIR` out-parameter, which names the family that claimed the
  topic and records "<family>: <reason>" for every recognizer that declined.
  No Markdown is rendered. The production route is unchanged: the trace is
  only filled when requested, and the recognizers receive no error sinks
  otherwise, exactly as before.
- Tool: `bootrace <book.boo> --coverage` prints one TSV row per topic
  (`id`, `level`, `route`, `family`, `reason`, `class`, `signature`) and a
  `# summary` line.
- Gate: `typed_route_inventory_test` (CTest label `slow`) runs the inventory
  over every fixture, prints the per-book table, and fails if the corpus total
  or any per-book typed count drops below the baseline committed in
  `libgeist/tests/typed_route_inventory.cpp`. Raising coverage means raising
  the baseline in the same commit. Uncontended it takes about 11 minutes,
  of which about 9 minutes is `N2AH1MST.BOO` (the SRMSG message and trap
  catalog recognizers are slow on its 33 message topics); inside the
  `ctest -L slow -j4` gate it took 1200 s.
- Structural classes and feature signatures come from the typed Control IR
  and Layout IR only: `BookControlKind` counts (CSELECT, CFONT, SRTBL, CMENU/
  CMITEM/CEMENU, SRMSG), SR opcodes (`SRFIG*`), CSELECT targets resolved
  against the book's resource catalog (image selectors), the CHDLEVEL operand
  (generated and front-matter kinds), and physical row start kinds. The one
  heuristic on visible row text is list detection: a row whose first visible
  word is a bullet (`o`, `-`, `*`, `°`, `•`) or an ordinal (`1.`, `a)`)
  followed by more text. '?' placeholder rows (`PhysicalRowStartKind::
  placeholder_wrap`) are counted but are not table evidence, because the same
  placeholder marks soft display-line wraps in ordinary prose.
- Byte identity: `boorender` output for 21 topics across the typed families
  (SC31-711 BACK_2, COMMENTS, 5.0, GLOSSARY, BACK_1.7, 1.0; N2AH1MST 22.0,
  23.0; FA1PLMM0 CONTENTS, 1.0, FIGURES; GG24-395 FIGURES; GG24-4302-00 8.5.3,
  PREFACE.2; QSYSNEWG 2.1; QS3X36CM 1.0, CONTENTS; packet 1.1; PRG1SORT 1.2;
  SC26-457 1.0; SH12-565 1.0; XWEBDEMO 1.0) is identical before and after
  this change; fast tier 43/43 and slow gate 15/15 (14 existing + the new
  ratchet) pass.

## Result

**71 typed, 7,291 legacy, 7,362 TOC topics (0.96 % typed).** This confirms
the 71 in the issue correction; the total differs from the 7,308 reported
there because this census counts every TOC entry with a topic header, while
the earlier number counted rendered export files.

## Per-book table

| Book | Typed | Legacy | Total | Typed families |
|---|---:|---:|---:|---|
| ACPZMST1.boo | 0 | 200 | 200 |  |
| DREICMST.boo | 1 | 371 | 372 | generated list 1 |
| FA1PLMM0.boo | 2 | 418 | 420 | generated list 1, menu 1 |
| GC23-046.boo | 2 | 97 | 99 | generated list 2 |
| GC28-183.boo | 1 | 145 | 146 | generated list 1 |
| GG24-395.boo | 6 | 220 | 226 | generated list 2, publication catalog 4 |
| GG24-4302-00.boo | 2 | 227 | 229 | generated list 2 |
| GX27-3999-00.boo | 0 | 36 | 36 |  |
| IBMMMSTR.boo | 0 | 60 | 60 |  |
| IEAC6MST.BOO | 1 | 206 | 207 | generated list 1 |
| ITPPIBOK.BOO | 5 | 251 | 256 | fixed prose 2, generated list 2, publication catalog 1 |
| N2AH1MST.BOO | 0 | 54 | 54 |  |
| OFCUSEOV.BOO | 0 | 201 | 201 |  |
| PRG1SORT.boo | 0 | 207 | 207 |  |
| QS3X36CM.BOO | 0 | 10 | 10 |  |
| QSYSINFO.BOO | 1 | 413 | 414 | publication catalog 1 |
| QSYSNEWG.BOO | 0 | 159 | 159 |  |
| SC09-138.boo | 3 | 533 | 536 | generated list 2, publication catalog 1 |
| SC09-2417-00.boo | 0 | 345 | 345 |  |
| SC24-546.boo | 2 | 319 | 321 | generated list 2 |
| SC24-5520-00.boo | 0 | 658 | 658 |  |
| SC24-5527-02.boo | 2 | 310 | 312 | generated list 2 |
| SC26-457.boo | 1 | 360 | 361 | generated list 1 |
| SC28-1881-05.boo | 1 | 90 | 91 | generated list 1 |
| SC31-605.boo | 2 | 108 | 110 | publication catalog 2 |
| SC31-711.boo | 28 | 54 | 82 | comment delivery 2, glossary 1, message catalog 1, publication catalog 14, trap catalog 10 |
| SC33-033.boo | 3 | 233 | 236 | generated list 2, menu 1 |
| SC34-425.boo | 0 | 257 | 257 |  |
| SC41-485.boo | 0 | 36 | 36 |  |
| SG24-204.boo | 2 | 91 | 93 | generated list 1, publication catalog 1 |
| SH12-565.boo | 1 | 289 | 290 | menu 1 |
| SH20-918.boo | 2 | 199 | 201 | generated list 2 |
| XWEBDEMO.boo | 1 | 12 | 13 | generated list 1 |
| packet.boo | 2 | 122 | 124 | generated list 2 |
| **Total** | **71** | **7291** | **7362** | |

### Typed topics by family

| Typed family | Topics |
|---|---:|
| generated list | 28 |
| publication catalog | 24 |
| trap catalog | 10 |
| menu | 3 |
| fixed prose | 2 |
| comment delivery | 2 |
| message catalog | 1 |
| glossary | 1 |

## Structural classification of the 7,291 legacy topics

One class per topic. Precedence: generated reader topics, then front matter, then the body feature set; exactly one body feature takes that feature's class, two or more are "mixed". Representatives are the first topic in each of three different books.

| Structural class (legacy topics) | Topics | Representatives (book:topic) |
|---|---:|---|
| heading + prose paragraphs only | 2650 | ACPZMST1.boo:FRONT_1.1, DREICMST.boo:FRONT_1.1, FA1PLMM0.boo:FRONT_1 |
| mixed | 1923 | ACPZMST1.boo:1.1.3, DREICMST.boo:PREFACE.3, FA1PLMM0.boo:1.0 |
| menu | 1059 | ACPZMST1.boo:FRONT_1, DREICMST.boo:FRONT_1, FA1PLMM0.boo:PREFACE |
| contains selectors/cross-references | 856 | ACPZMST1.boo:1.2.3.2, DREICMST.boo:PREFACE.2, FA1PLMM0.boo:CHANGES.1 |
| contains fixed rows/tables | 426 | ACPZMST1.boo:FRONT_1.2, GC23-046.boo:3.2, GG24-395.boo:FRONT_1 |
| contains figures/images | 138 | ACPZMST1.boo:2.4.1.2, DREICMST.boo:1.5.6.5.1, FA1PLMM0.boo:PREFACE.3 |
| title/edition front matter | 91 | ACPZMST1.boo:COVER, DREICMST.boo:COVER, FA1PLMM0.boo:COVER |
| generated (TOC/INDEX/FIGURES/TABLES) | 63 | ACPZMST1.boo:CONTENTS, DREICMST.boo:CONTENTS, FA1PLMM0.boo:CONTENTS |
| prose + lists | 45 | DREICMST.boo:A.3, FA1PLMM0.boo:6.2.2, GC28-183.boo:6.2 |
| messages | 33 | N2AH1MST.BOO:4.0, SC31-711.boo:4.3.5, SC34-425.boo:2.4.7 |
| title only (no body rows) | 7 | GG24-395.boo:3.3.17.1, IBMMMSTR.boo:PREFACE.6.5, SC24-5527-02.boo:6.2 |

### Body feature signatures (top 15)

The raw feature set without the precedence rules, so mixed topics can be sized by combination.

| Body feature signature (legacy topics) | Topics | Representatives |
|---|---:|---|
| prose | 2782 | ACPZMST1.boo:COVER, DREICMST.boo:COVER, FA1PLMM0.boo:COVER |
| menu | 1063 | ACPZMST1.boo:FRONT_1, DREICMST.boo:FRONT_1, FA1PLMM0.boo:PREFACE |
| selectors | 875 | ACPZMST1.boo:EDITION, DREICMST.boo:EDITION, FA1PLMM0.boo:CHANGES.1 |
| tables | 426 | ACPZMST1.boo:FRONT_1.2, GC23-046.boo:3.2, GG24-395.boo:FRONT_1 |
| selectors+figures | 399 | ACPZMST1.boo:1.1.3, DREICMST.boo:1.1.1.1, FA1PLMM0.boo:1.1 |
| tables+selectors | 393 | ACPZMST1.boo:1.2.4.4, GC23-046.boo:2.1, GG24-395.boo:3.3.6.2 |
| selectors+menu | 331 | ACPZMST1.boo:3.14, DREICMST.boo:PREFACE.3, FA1PLMM0.boo:1.0 |
| figures | 139 | ACPZMST1.boo:2.4.1.2, DREICMST.boo:1.5.6.5.1, FA1PLMM0.boo:PREFACE.3 |
| selectors+figures+menu | 125 | DREICMST.boo:1.1.1, FA1PLMM0.boo:2.5.1, GG24-395.boo:1.1.3 |
| tables+selectors+figures | 112 | DREICMST.boo:1.2.1, FA1PLMM0.boo:2.1.3, GC23-046.boo:6.9.5.1 |
| tables+figures | 95 | ACPZMST1.boo:2.1.2, DREICMST.boo:1.3.2, FA1PLMM0.boo:2.7.11 |
| lists+selectors+figures | 65 | DREICMST.boo:1.4.1.3, FA1PLMM0.boo:8.6.3, GC23-046.boo:6.9.4 |
| tables+selectors+menu | 60 | ACPZMST1.boo:4.0, GC23-046.boo:6.9.3, GG24-395.boo:3.2.1 |
| tables+figures+menu | 60 | GC28-183.boo:2.2, IEAC6MST.BOO:5.2, OFCUSEOV.BOO:PREFACE |
| figures+menu | 49 | IEAC6MST.BOO:9.2, PRG1SORT.boo:1.1.8, QSYSNEWG.BOO:5.2 |

### Classes per book

| Book | prose only | mixed | menu | selectors/cross-references | fixed rows/tables | figures/images | title/edition front matter | generated | prose + lists | messages | title only |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| ACPZMST1.boo | 45 | 50 | 32 | 26 | 38 | 5 | 2 | 2 | 0 | 0 | 0 |
| DREICMST.boo | 152 | 151 | 29 | 28 | 0 | 6 | 2 | 2 | 1 | 0 | 0 |
| FA1PLMM0.boo | 167 | 112 | 66 | 55 | 0 | 12 | 2 | 2 | 2 | 0 | 0 |
| GC23-046.boo | 29 | 35 | 18 | 8 | 2 | 0 | 3 | 2 | 0 | 0 | 0 |
| GC28-183.boo | 49 | 29 | 59 | 3 | 0 | 0 | 2 | 2 | 1 | 0 | 0 |
| GG24-395.boo | 76 | 71 | 37 | 25 | 5 | 0 | 3 | 2 | 0 | 0 | 1 |
| GG24-4302-00.boo | 107 | 28 | 76 | 9 | 2 | 0 | 3 | 2 | 0 | 0 | 0 |
| GX27-3999-00.boo | 5 | 11 | 5 | 8 | 2 | 0 | 3 | 2 | 0 | 0 | 0 |
| IBMMMSTR.boo | 29 | 11 | 14 | 1 | 0 | 0 | 2 | 1 | 1 | 0 | 1 |
| IEAC6MST.BOO | 65 | 83 | 27 | 20 | 0 | 5 | 3 | 2 | 1 | 0 | 0 |
| ITPPIBOK.BOO | 79 | 34 | 48 | 84 | 0 | 0 | 3 | 2 | 1 | 0 | 0 |
| N2AH1MST.BOO | 7 | 14 | 5 | 1 | 6 | 1 | 3 | 1 | 0 | 16 | 0 |
| OFCUSEOV.BOO | 87 | 29 | 27 | 34 | 0 | 0 | 3 | 2 | 19 | 0 | 0 |
| PRG1SORT.boo | 73 | 71 | 34 | 17 | 5 | 3 | 2 | 2 | 0 | 0 | 0 |
| QS3X36CM.BOO | 1 | 2 | 0 | 0 | 4 | 0 | 2 | 1 | 0 | 0 | 0 |
| QSYSINFO.BOO | 330 | 11 | 10 | 2 | 55 | 0 | 3 | 2 | 0 | 0 | 0 |
| QSYSNEWG.BOO | 46 | 71 | 24 | 7 | 0 | 5 | 3 | 2 | 1 | 0 | 0 |
| SC09-138.boo | 174 | 164 | 51 | 92 | 24 | 23 | 3 | 2 | 0 | 0 | 0 |
| SC09-2417-00.boo | 190 | 65 | 47 | 29 | 5 | 0 | 3 | 2 | 4 | 0 | 0 |
| SC24-546.boo | 117 | 43 | 34 | 108 | 9 | 0 | 4 | 2 | 2 | 0 | 0 |
| SC24-5520-00.boo | 255 | 151 | 60 | 56 | 95 | 34 | 3 | 2 | 2 | 0 | 0 |
| SC24-5527-02.boo | 15 | 150 | 43 | 26 | 70 | 0 | 3 | 2 | 0 | 0 | 1 |
| SC26-457.boo | 157 | 90 | 69 | 30 | 0 | 4 | 3 | 2 | 1 | 0 | 4 |
| SC28-1881-05.boo | 9 | 66 | 6 | 3 | 0 | 1 | 3 | 2 | 0 | 0 | 0 |
| SC31-605.boo | 7 | 85 | 5 | 4 | 1 | 1 | 2 | 2 | 1 | 0 | 0 |
| SC31-711.boo | 18 | 15 | 9 | 6 | 0 | 0 | 3 | 2 | 0 | 1 | 0 |
| SC33-033.boo | 55 | 52 | 25 | 7 | 84 | 1 | 2 | 1 | 6 | 0 | 0 |
| SC34-425.boo | 44 | 74 | 81 | 30 | 1 | 6 | 3 | 2 | 0 | 16 | 0 |
| SC41-485.boo | 11 | 15 | 1 | 6 | 0 | 0 | 1 | 2 | 0 | 0 | 0 |
| SG24-204.boo | 13 | 31 | 26 | 5 | 4 | 7 | 3 | 2 | 0 | 0 | 0 |
| SH12-565.boo | 101 | 63 | 48 | 48 | 0 | 23 | 3 | 2 | 1 | 0 | 0 |
| SH20-918.boo | 86 | 16 | 28 | 52 | 10 | 1 | 3 | 2 | 1 | 0 | 0 |
| XWEBDEMO.boo | 1 | 5 | 0 | 4 | 0 | 0 | 1 | 1 | 0 | 0 | 0 |
| packet.boo | 50 | 25 | 15 | 22 | 4 | 0 | 4 | 2 | 0 | 0 | 0 |

## Rejection reasons

Every legacy topic carries the reasons from each recognizer that declined it, normalized by collapsing numbers, dotted topic numbers, hex offsets and quoted material. Top 20 of 91 distinct reasons:

| Recognizer declined reason (normalized) | Topics |
|---|---:|
| `publication catalog: declined` | 7195 |
| `comment delivery: comment source envelope is absent or ambiguous` | 7187 |
| `glossary: glossary topic has no SRGLS boundary` | 7171 |
| `menu source: source does not contain one complete non-empty menu` | 5415 |
| `fixed prose: fixed prose topic must occupy exactly one logical record` | 4174 |
| `fixed prose: topic has controls or content outside the fixed prose envelope` | 2573 |
| `menu targets: raw menu label differs from canonical catalog title: #` | 1356 |
| `fixed prose: pre-prose segment is not a source anchor` | 195 |
| `fixed prose: inner fixed prose rejected: ST payload is empty or placeholder-framed` | 179 |
| `topic ownership rejected: source cell received incompatible ownership dispositions` | 96 |
| `menu targets: raw menu label differs from canonical catalog title beyond its compact terminal token: #` | 90 |
| `menu targets: raw menu label differs from canonical catalog title: APPENDIX#` | 56 |
| `message catalog: message topic has no numeric SRMSG catalog boundary` | 33 |
| `menu targets: raw menu label differs from canonical catalog title: PREFACE.#` | 32 |
| `fixed prose: topic metadata control contains trailing content` | 29 |
| `fixed prose: inner fixed prose rejected: ST payload lacks repeated source-proven prose rows` | 29 |
| `menu targets: raw menu label differs from canonical catalog title: A.#` | 26 |
| `menu targets: raw menu label differs from canonical catalog title: B.#` | 26 |
| `menu: menu topic CMENU boundary is missing or has content` | 25 |
| `menu targets: raw menu label differs from canonical catalog title: FRONT_#` | 23 |

## Reading the census

- The per-book baseline for the ratchet is the "Typed" column above.
- "menu" means the topic body carries a CMENU/CMITEM/CEMENU child-topic list;
  1,059 legacy topics are parent topics whose only non-prose structure is that
  menu, and a further 331 combine the menu with cross-reference selectors.
  The typed `menu` family currently admits only three of them, mostly because
  the raw menu label differs from the canonical catalog title (1,356 topics,
  see the reason table) or the source contains prose around the menu (5,415
  topics decline "one complete non-empty menu").
- "heading + prose paragraphs only" (2,650 topics; 2,782 by body signature
  before front-matter/generated precedence) is the largest single slice and
  has no typed family at all today: `fixed prose` requires the topic to be a
  single logical record (4,174 declines) with nothing outside its envelope
  (2,573 declines). A generic multi-record prose lowering is the highest-value
  next slice, followed by CSELECT cross-reference inlines (856 topics are
  prose plus selectors, and selectors occur in most of the 1,923 mixed topics) and the
  CMENU child list.
- "contains fixed rows/tables" counts SRTBL-bounded tables only (426 topics,
  concentrated in SC24-5520-00, SC33-033, SC24-5527-02, QSYSINFO).
- "contains figures/images" counts SRFIG blocks and CSELECT image selectors
  (138 topics alone; ~900 more in mixed signatures).
- `topic ownership rejected: source cell received incompatible ownership
  dispositions` (96 topics) is the only pre-family rejection: those topics
  never reach a recognizer and need an ownership-IR fix first.
- No topic was claimed by a family and then rejected at verification; every
  legacy topic is a recognizer decline.

## Reproducing

```
cmake -S libgeist -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4
for b in BOO/*.boo BOO/*.BOO; do build/bootrace "$b" --coverage > "$(basename "$b").tsv"; done
build/typed_route_inventory_test   # prints the per-book table and the ratchet result
```
