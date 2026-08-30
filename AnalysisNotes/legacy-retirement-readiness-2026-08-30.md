# Legacy renderer retirement readiness (2026-08-30)

Measurement and decision memo for issue #58: *can the legacy string renderer be
retired now?* Baseline is `main` `e50c60f`, Release build, all 34 fixtures.

Retiring legacy does not require 100% typed coverage. It requires every topic to
have an acceptable non-legacy rendering. This note measures whether the
`best-effort` route — the topic's own decoded display lines, verbatim — is that
rendering for the 326 topics that still fall back to legacy, and then answers
the four retirement gates.

Coverage at the time of measurement (from the per-book `render-diagnostics.tsv`
that `boo2git` writes, summed over all 34 fixtures):

| severity | topics |
| --- | ---: |
| `typed` | 7,031 |
| `typed-degraded` | 1 |
| `legacy-fallback` | **326** |
| `best-effort` | 4 |
| `failed` | 0 |

## Method

Three corpora were exported and compared against the hosted BookServer.

1. **Baseline** — `boo2git` over all 34 fixtures, unmodified `main`.
2. **Forced best-effort** — a temporary switch in `TocEntry::render()`
   (`GEIST_FORCE_BEST_EFFORT`) that, for any topic the typed dispatcher
   declines, **does not call the legacy renderer at all**: the Markdown becomes
   the topic's heading (built from `TopicIdentityIR`, exactly the information
   the TOC already carries) plus the `best-effort` verbatim block. The switch
   is env-gated, and with the variable unset the build was verified to produce a
   byte-identical export to the baseline. It was removed before commit.
3. **Hosted** — the reference rendering of each of the 326 topics, fetched with
   the verified short-name/DT map in
   `AnalysisNotes/verbatim-fixed-layout-regions-2026-08-30.md`.

322 of the 326 could be compared; the remaining 4 are in `XWEBDEMO`, which the
hosted library does not carry.

Two metrics, both against hosted:

- **Words lost** — multiset difference of word occurrences, hosted minus ours.
  This measures *content* recovery and is insensitive to line breaking.
- **Line-for-line agreement** — the fraction of hosted's non-blank `<pre>` block
  lines that appear verbatim as a line of our Markdown. This measures *shape*.
  Hosted's opening tag is always `<pre width="…">`.

One correction worth recording, because it inverted an intermediate result: the
first comparison script stripped HTML with `<[^>]*>`, and `[^>]` matches
newlines. Box-drawing art contains bare `<` characters (`>>__LUNAME=…>< |`), so
the regex swallowed whole runs of lines and reported best-effort as losing 1,632
words on `SH12-565` `4.7.5.1`, content that is in fact present. Restricting the
pattern to `</?[a-zA-Z!][^<>\n]*>` fixed it. Every number below is post-fix.

## Result: best-effort versus legacy, against hosted

Totals over the 322 comparable topics (572,828 hosted words, 75,044 hosted
`<pre>` lines):

| | legacy | best-effort |
| --- | ---: | ---: |
| hosted words lost | 33,280 (5.81%) | **2,223 (0.39%)** |
| hosted words added that hosted does not have | 4,851 (0.85%) | 19,523 (3.41%) |
| hosted `<pre>` lines matched verbatim | 316 (0.4%) | **72,458 (96.6%)** |
| topics losing zero words | 86 | **159** |
| topics losing more than 5% of their words | 69 | **33** |
| topics matching *every* hosted `<pre>` line | 4 (of 313) | **134 (of 313)** |

Per-topic verdict, combining both metrics (better on one and not worse on the
other):

| verdict | topics |
| --- | ---: |
| best-effort **better** | **256** |
| mixed (better on one metric, worse on the other) | 48 |
| equal | 3 |
| best-effort **worse** | **15** |

Best-effort recovers content legacy drops at a scale that is not marginal. The
largest single case is `N2AH1MST` `28.0`, where legacy loses **20,988** of
139,704 hosted words and best-effort loses 86. Other named examples:
`SC34-425 APPENDIX1.5.3` (509 → 8), `SC24-5527-02 G.3` (476 → 0),
`FA1PLMM0 9.3` (448 → 2), `GX27-3999-00 B.0` (423 → 5),
`N2AH1MST 16.0` (413 → 7), `SC09-138 B.0` (360 → 15),
`OFCUSEOV 1.1` (243 → 0), `SH20-918 3.16` (210 → 0).

### The 15 topics where best-effort is worse, characterised

They fall into exactly two classes, neither of which is a loss of book text.

**Class A — the reader-generated subtopic menu (10 topics).**
`IBMMMSTR 1.6` (34 words), `IBMMMSTR 1.12` (13), `SC26-457 3.14.2` (12),
`IBMMMSTR PREFACE.6` (10), `SC33-033 A.3` (8), `SC26-457 1.3` (7),
`SC09-2417-00 4.3.6` (7), `SC09-2417-00 3.3` (4), `SC09-2417-00 3.5.5` (2),
`SC31-711 BACK_1.12` (1). Legacy emits hosted's `Subtopics:` list with the
child topic numbers and working links; best-effort emits the raw selector rows,
which carry the labels but not the numbers, which no BOO byte states — the
reader generates them. The "lost words" are those generated numbers. The real
cost is navigation, not text: across all 322 topics, best-effort output carries
**2,050 fewer Markdown links** than legacy output. 71 of the 322 topics carry a
generated menu; the same class accounts for the largest "mixed" regression,
`QSYSINFO 2.1` (101 generated numbers lost, 17 more hosted `<pre>` lines
matched, and real prose recovered — bullets, the word `alphabetically`).

**Class B — cover and title pages (5 topics).** `packet COVER`, `packet TITLE`,
`GX27-3999-00 COVER`, `SC41-485 COVER`, `SC09-2417-00 COVER`. Both routes lose
the same words; best-effort matches fewer hosted lines because it keeps the
region's own left indent (hosted strips it on these pages) and leaks the
unparsed directive rows `OFF COVER` / `OFF ECOVER 0 0` into the block.

### Best-effort's own two defects, quantified

- **Leaked control rows.** 253 of the 322 topics leak rows that hosted does not
  display: **2,303 `SI …` index-term rows** and **385 `OFF …` rows** from `cz`
  directives whose segmentation failed. This is the whole of best-effort's
  19,523 added words. `Format/markup.md` already establishes that `SI` occupies
  exactly one display line and displays nothing, so suppressing `SI` **control
  segments** in `best_effort_lines` is a documented, exact fix; simulated by
  filtering the rows out of the exported Markdown it removes 11,368 of the
  19,523 added words (58%) and changes the line-match rate not at all.
- **No generated navigation.** Class A above.

### Per-book word loss

| book | hosted words | legacy lost | best-effort lost |
| --- | ---: | ---: | ---: |
| N2AH1MST | 310,443 | 22,370 (7.2%) | 450 (0.1%) |
| SC34-425 | 56,659 | 598 (1.1%) | 12 (0.0%) |
| SC09-138 | 32,856 | 1,416 (4.3%) | 416 (1.3%) |
| SC24-5520-00 | 21,707 | 700 (3.2%) | 43 (0.2%) |
| SC33-033 | 21,255 | 572 (2.7%) | 95 (0.4%) |
| SC09-2417-00 | 18,281 | 718 (3.9%) | 207 (1.1%) |
| SC24-5527-02 | 17,083 | 2,077 (12.2%) | 58 (0.3%) |
| SC24-546 | 16,544 | 314 (1.9%) | 103 (0.6%) |
| OFCUSEOV | 13,837 | 832 (6.0%) | 8 (0.1%) |
| SH12-565 | 8,490 | 282 (3.3%) | 83 (1.0%) |
| SH20-918 | 7,786 | 436 (5.6%) | 32 (0.4%) |
| FA1PLMM0 | 7,287 | 752 (10.3%) | 29 (0.4%) |
| GX27-3999-00 | 5,919 | 664 (11.2%) | 47 (0.8%) |
| SC28-1881-05 | 5,900 | 173 (2.9%) | 15 (0.3%) |
| GG24-395 | 5,305 | 399 (7.5%) | 103 (1.9%) |
| IEAC6MST | 3,515 | 249 (7.1%) | 21 (0.6%) |
| SG24-204 | 3,243 | 249 (7.7%) | 117 (3.6%) |
| ACPZMST1 | 2,321 | 22 (0.9%) | 2 (0.1%) |
| QSYSNEWG | 2,247 | 45 (2.0%) | 3 (0.1%) |
| QSYSINFO | 1,971 | 59 (3.0%) | 156 (7.9%) |
| SC26-457 | 1,806 | 17 (0.9%) | 30 (1.7%) |
| ITPPIBOK | 1,762 | 5 (0.3%) | 9 (0.5%) |
| PRG1SORT | 1,278 | 17 (1.3%) | 8 (0.6%) |
| SC31-605 | 1,249 | 3 (0.2%) | 2 (0.2%) |
| SC31-711 | 978 | 3 (0.3%) | 5 (0.5%) |
| IBMMMSTR | 645 | 146 (22.6%) | 57 (8.8%) |
| SC41-485 | 640 | 48 (7.5%) | 66 (10.3%) |
| packet | 519 | 46 (8.9%) | 43 (8.3%) |
| GG24-4302-00 | 539 | 0 (0.0%) | 0 (0.0%) |
| GC28-183 | 469 | 34 (7.2%) | 3 (0.6%) |
| DREICMST | 294 | 34 (11.6%) | 0 (0.0%) |

Six books get worse under best-effort, all small: `QSYSINFO`, `SC41-485`,
`SC26-457`, `ITPPIBOK`, `SC31-711`, and `packet` — and in every one of them the
difference is Class A or Class B above, not lost prose.

## The four retirement gates

### Gate 1 — the link map. **OPEN, and blocking.**

`boo2git`'s `build_markdown_link_map` still calls `TocEntry::gml_records()` for
every TOC entry, so the legacy renderer runs for all 7,362 topics. `origin/main`
has not moved past `e50c60f`: the typed link-map slice has **not** landed as of
this measurement.

Re-measured independently with a temporary switch that skips the legacy GML for
topics whose route is `typed`: **1,760 corpus files change**, and the corpus's
unresolved `](<#…>)` link count rises from **7,401 to 12,267**. Typed Markdown's
cross-references resolve through legacy's `:anchor` / `:fig` / `:table` /
`:image` records; without them a resolved `[Figure 1](1-1-3.md#OVER)` degrades
to `[Figure 1](<#FIGOVER>)`.

Legacy cannot be deleted until anchor, figure, table and image discovery is
served from the typed IR. Nothing else in this note changes that.

### Gate 2 — the 326. **CLOSED by measurement, with two named fixes.**

Best-effort is better than legacy on 256 of 322, equal on 3, worse on 15, mixed
on 48; it loses 0.39% of hosted words against legacy's 5.81%, and reproduces
96.6% of hosted's `<pre>` lines against legacy's 0.4%. Every "worse" case is
generated navigation or cover-page line shape, not lost book text.

Two fixes should land with the switch-over, both small and both measured above:
suppress `SI` control segments in `best_effort_lines` (2,303 rows, 253 topics),
and emit the topic's child menu from the TOC above the verbatim block (71
topics, 2,050 links). Neither is a precondition for content fidelity; both are
preconditions for not regressing navigation.

### Gate 3 — `boorender <book> --raw|--md`. **The gate that decides the prize.**

The whole-book mode is the only caller of `BooDocument::raw_gml_records()` and
`BooDocument::markdown()`; no test and not `boo2git` reaches them. It is also a
distinct code path from `TocEntry::gml_records()`: it decodes each topic's
record range and runs `render_gml_records` over the concatenation, then
`render_markdown_records` over the result.

What removing it loses: a single-stream whole-book GML dump and a single-stream
whole-book Markdown rendering. Both are legacy-quality — `boorender
BOO/SC31-711.boo --md` reproduces exactly the losses measured above, including
run-together paragraphs and stray `<` glyphs. The per-topic forms
(`boorender <book> <topic> --raw`) go with it, since `TocEntry::gml_records()`
is the legacy projection itself.

Serving it from the typed route is a small piece of work in the example, not in
the library: iterate `document.table_of_contents()` and concatenate
`entry.markdown()`, which is the typed rendering with its diagnostics. There is
no typed replacement for `--raw`, and none is needed: `bootrace <book> <topic>
--records|--segments|--ir|--tokens|--lines` is the typed inspection channel and
is strictly more informative than a GML projection.

### Gate 4 — test pins.

One real content pin, five stability pins, and three direct legacy unit-test
files.

| pin | what it asserts | lost if legacy goes? |
| --- | --- | --- |
| `document_ir_synthetic.cpp:169-172` | a bare `TocEntry` with `raw_records = {":p.Public compatibility entry"}` renders `markdown()` as `"Public compatibility entry\n"` | **yes, deliberately** — this is the public "build a TocEntry by hand from GML strings and render it" contract. It is the only pin that encodes behaviour rather than stability. Removing legacy removes that contract; nothing in `boo2git`, `bootrace` or the corpus uses it. |
| `topic_document_lowering.cpp:48-83` | `gml_records()` is unchanged before and after typed rendering (4 topics) | no — pure non-mutation guard, deleted with the API |
| `glossary_production.cpp:37-42` | same | no |
| `menu_production.cpp:78-103` | same | no |
| `message_production.cpp:49-74` | same | no |
| `generated_list_production.cpp:78-90` | same | no |
| `markup_synthetic.cpp` (603 lines) | `render_gml_records` / `render_gml_records_with_source_layout` behaviour directly | deleted with the renderer; it reaches **122 lines of `markup.cpp` nothing else executes** |
| `markup_debug.cpp` (75 lines) | `render_gml_records({"CPICTURE 1 PIC1"})` | deleted with the renderer |
| `implicit_grid.cpp:114-120` | `render_gml_records_with_source_layout` on grid rows | deleted with the renderer |
| `lazy_open_gg24.cpp:17`, `lazy_open_qs3x36cm.cpp:26` | `raw_records.empty()` — lazy-loading, not rendering | no, they keep working |

No pin encodes a *format* fact that would be lost; `markup_synthetic.cpp`'s
subject matter (GML projection spelling) has no meaning once the projection is
gone. The one contract that genuinely disappears is the public
`TocEntry::raw_records` → `markdown()` compatibility entry.

## The prize, measured

`--coverage` build of **both** `geist` and `geist_static` (tests link the static
library), instrumenting `markup.cpp`, `markdown.cpp` and `toc.cpp`. Four
workloads were run with the counters reset between each: the `boo2git` corpus;
`boorender <book> --raw` and `--md` for all 34 fixtures; the whole `ctest` suite
(50 fast + 15 slow, all passing); and the `boo2git` corpus again with legacy
fully bypassed (both temporary switches on, so `render_legacy_topic_markdown`
and the legacy link map are never entered).

Executable (gcov-instrumented) lines:

| workload | markup.cpp | markdown.cpp | toc.cpp | total |
| --- | ---: | ---: | ---: | ---: |
| instrumented | 4,358 | 1,605 | 885 | 6,848 |
| `boo2git` corpus | 4,070 | 1,293 | 853 | 6,216 |
| `boorender <book> --raw\|--md` | 3,662 | 1,219 | 163 | 5,044 |
| `ctest` (all tests) | 3,823 | 1,307 | 851 | 5,981 |
| **live today** (union of the three) | 4,194 | 1,430 | 853 | **6,477** |
| never executed by anything today | 164 | 175 | 32 | 371 |
| `boo2git` with legacy fully bypassed | 3,352 | 69 | 737 | 4,158 |

Two retirement scenarios:

| scenario | markup.cpp | markdown.cpp | toc.cpp | becomes unreachable |
| --- | ---: | ---: | ---: | ---: |
| **A** — retire legacy *and* drop whole-book `--raw`/`--md` | 842 | 1,369 | 116 | **2,327 executable lines** |
| **B** — retire legacy, keep whole-book `--raw`/`--md` as it is | 196 | 150 | 116 | **462 executable lines** |

Add the 371 lines nothing executes today and scenario A retires **2,698 of
6,848 executable lines (39%)** across the three files — roughly 3,900 physical
lines of the 9,869 in `markup.cpp` + `markdown.cpp` + `toc.cpp`, at the
0.69 executable-to-physical ratio these files carry.

The whole-book mode alone is what keeps ~1,865 executable lines of legacy
reachable. Two cross-checks confirming the earlier over-deletion warning:
**123 lines of `markdown.cpp` are reached only by `boorender --raw|--md`** and
by nothing else, and **136 lines (122 in `markup.cpp`) are reached only by the
test suite**. Measuring `boo2git` alone would call both sets dead.

## Recommendation

**Retire after three specific fixes — not yet, but the remaining typed-coverage
work is no longer blocking.**

The evidence is unambiguous on the question the issue was actually asking. For
the 326 topics that still route through legacy, the `best-effort` route
available today is *better than legacy against the reference renderer* on 256 of
322, losing 15× fewer words (0.39% versus 5.81%) and reproducing 96.6% of
hosted's `<pre>` lines against legacy's 0.4%. Legacy is not a safety net for
these topics; it is the lossier of the two options we already have. **Driving
typed coverage from 95.5% toward 100% is therefore optional work, not a
precondition for deleting legacy.** It remains worth doing for structure,
provenance and links, but no topic is waiting on it for its words.

What blocks retirement is entirely mechanical:

1. **Gate 1 — move link discovery to the typed IR.** Measured cost of not doing
   it: 1,760 degraded files, 4,866 additional unresolved anchors. A slice was
   reported in flight but has not landed on `origin/main`.
2. **Two best-effort fixes**, both scoped and both measured here: suppress `SI`
   control segments (2,303 leaked rows across 253 topics, exactness already
   established in `Format/markup.md`) and emit the child menu from the TOC above
   the verbatim block (71 topics, 2,050 links).
3. **Decide Gate 3.** Reserving `boorender <book> --raw|--md` as-is caps the
   deletion at 462 lines and keeps essentially the whole legacy renderer alive
   for a mode with no test and no consumer. Re-serving `--md` from the typed
   route (concatenate `entry.markdown()` over the TOC) and retiring `--raw` in
   favour of `bootrace` unlocks the other 1,865 lines. That is the
   recommendation.

With those three done, the deletion is 2,698 of 6,848 executable lines, the
`legacy-fallback` severity disappears from the ladder, and `best-effort` becomes
the honest floor it was designed to be. Gate 4 costs one public contract
(`TocEntry::raw_records` → `markdown()`), which nothing in this repository uses.

Nothing in this slice was shipped as code: both fixes above are zero-effect on
today's corpus (the four current `best-effort` topics leak no `SI` rows), so
they belong with the switch-over that makes them observable, not ahead of it.

## Reproducing

Scripts used for this measurement lived in the slice's scratch directory and
are not committed; the procedure is:

1. Export the baseline corpus with `boo2git` for all 34 fixtures and read the
   326 `legacy-fallback` rows out of each book's `render-diagnostics.tsv`.
2. Add an env-gated switch in `TocEntry::render()` that skips
   `render_legacy_topic_markdown` and forces the best-effort block for any topic
   with no typed document; verify the switch is inert when unset by diffing an
   export against the baseline.
3. Fetch each topic from hosted with the short-name/DT map in
   `AnalysisNotes/verbatim-fixed-layout-regions-2026-08-30.md`; decode
   latin-1; strip tags with `</?[a-zA-Z!][^<>\n]*>`, never `<[^>]*>`.
4. Compare word multisets and `<pre>` line sequences as described above.
5. For the coverage figures, configure with `--coverage -O0 -g` on
   `markup.cpp`, `markdown.cpp`, `toc.cpp` for both `geist` and `geist_static`,
   reset `*.gcda` between workloads, and read line hits with
   `gcov --json-format --stdout`.
