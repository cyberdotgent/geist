# Root causes of the 274 declining topics (2026-08-30)

Re-derivation of the cause taxonomy behind issues #60, #61, #62, #63, #64 and
#67, which were grouped by *decline message*. The decline message names
whichever check fired first, not the missing model, so the grouping does not
survive contact with the source.

Everything below is measured over the whole corpus (33 books, 7,430 topics,
274 declining) unless a sample size is given.

## Reproduction

```
cmake -S libgeist -B build_rls -DCMAKE_BUILD_TYPE=Release
cmake --build build_rls --target bootrace -j 8
for f in BOO/*.boo BOO/*.BOO; do build_rls/bootrace "$f" --coverage; done   # per book
```

Severity census over all 7,430 topics:

| severity | topics |
| --- | ---: |
| `typed` | 7,077 |
| `typed-degraded` | 11 |
| `best-effort` (declining) | 274 |
| (generated navigation, no severity) | 68 |

## The hosted control

For each of the 274 declining topics the hosted BookServer page was fetched
(270 succeeded; the 4 `XWEBDEMO` topics are not on the shelf) and its **body**
— between the first `<hr>` and the footer `<hr>`, with the `Subtopics:` menu
excluded — was counted for `<ul>`, `<dl>`, `<H2..6>`, `<img>` and body
`<a href>`.

This is the measurement the earlier grouping was missing. It separates
"Geist declines and loses structure hosted has" from "Geist declines and
hosted is verbatim too".

| cause | topics | books | hosted `<ul>` | `<dl>` | `<dt>` | body links | subtopic menus |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| C1 undelimited drawn box-art rows | 60 | 10 | **0** | **0** | **0** | 196 | 10 |
| C8 segment boundary misplaced around a control | 39 | 13 | 0 | 0 | 0 | 607 | 3 |
| C5 message-catalogue blocks in prose topics | 30 | 6 | 4 | 0 | 0 | 155 | 0 |
| C13 topic metadata / ST envelope | 21 | 12 | 2 | 0 | 0 | 29 | 3 |
| C9 display-line length byte | 19 | 4 | 0 | 0 | 0 | 40 | 4 |
| C11 font/selector span geometry | 16 | 8 | 0 | 0 | 0 | 34 | 2 |
| C3 other `cz OFF` region unmodelled | 12 | 5 | 4 | 0 | 0 | 2 | 0 |
| C2 `cz OFF SYNTAX` unmodelled | 11 | 1 | **9** | **6** | **23** | 6 | 0 |
| C6 compiled menu labels not bounded | 10 | 8 | 2 | 0 | 0 | 5 | 10 |
| C15 unclaimed token / no disposition | 9 | 5 | 0 | 0 | 0 | 7 | 2 |
| C12 table envelope declined | 8 | 5 | 2 | 0 | 0 | 26 | 1 |
| C7 `cz` list opener carries display text | 6 | 2 | 0 | **6** | **57** | 7 | 1 |
| C10 figure region / inline picture (#65) | 6 | 4 | 0 | 0 | 0 | 39 | 0 |
| C4 `cz OFF LBLBOX` closure too strict | 6 | 2 | 0 | 0 | 0 | 2 | 5 |
| C14 empty `SI` index term | 5 | 1 | 0 | 0 | 0 | 84 | 1 |
| C17 implicit column grid | 5 | 3 | 0 | 0 | 0 | 5 | 0 |
| C20 remainder | 4 | 3 | 0 | 3 | 27 | 3 | 1 |
| C16 structural control not a bare anchor | 3 | 3 | 0 | 0 | 0 | 10 | 0 |
| C19 nested/misaligned list items | 2 | 2 | 0 | 0 | 0 | 0 | 1 |
| C18 selector malformed | 2 | 2 | 2 | 0 | 0 | 1 | 0 |
| **total** | **274** | | 25 | 15 | 107 | **1,258** | 46 |

Total hosted body `<img>` in declining topics: 91, of which 83 are C10 (#65).

## C1 is not what #60 says it is

#60 groups 64 topics under

```
prose topic rejected: placeholder run '??' is followed by visible text at record N token N
```

and states that a placeholder run "is the projection of bytes the decoder
could not map to characters".

**That premise is false.** `is_placeholder_run` (`prose_topic_stream.cpp:53`)
is true for a token whose every word is `U+2500..U+25FF` *or* the unmapped
sentinel. Dumping the named token for all 64 topics gives:

| words in the offending token | topics |
| --- | ---: |
| box drawing `U+2500..U+257F` | **64** |
| unmapped sentinel | 0 |

Not one of the 64 involves an undecodable byte. `SC09-2417-00 PREFACE.2.1`
record 29 token 177 is `values=9492,9472` — `└` `─`:

```
build_rls/bootrace BOO/SC09-2417-00.boo PREFACE.2.1 --tokens \
  | awk -F'\t' '$1==29' | grep 'token=177 '
29	token=177 value=50746 width=2 prefix=- bytes=[0x3935a,0x3935c) words='??' values=9492,9472
```

The `?` is only the one-byte ASCII projection printed in the diagnostic.
Rendering the offending display line for all 64 shows what they actually are:

```
SC24-546     1.3.1     |_optional_item_|                       railroad syntax diagram
SC24-5520-00 1.1.27     ________ _____ __________ ______       railroad syntax diagram
SC24-5527-02 1.0       ___ Note ______________________         drawn note box
IEAC6MST     2.1       __________________________________      drawn roadmap table
SC33-033     2.0       ---- General-Use Programming Interface ----   boundary rule
FA1PLMM0     11.3.2    |--- End of Product-Sensitive Programming Interface ---|
```

So the cause is **drawn box art on a display row that also carries words**,
and the model is right that this is not prose.

### Hosted agrees: these topics are verbatim there too

Over all 60 C1 topics hosted emits **0 `<ul>`, 0 `<dl>`, 0 `<dt>`** and 4
extra headings (in one topic). 59 of 60 are wrapped in a single
`<pre width="80">`; `packet GLOSSARY` uses `<pre width="132">`. Example
(`SC24-5466-04` `1.3.1` DT 19940323131240): the whole topic is one `<pre>`,
the bullets are literal `°`, the diagram is inline.

**There is no structure to recover in C1.** Geist's verbatim output is the
correct shape. What hosted does that Geist does not is keep **196 body
`<a href>` cross-references inside the `<pre>`**.

`render/ieac6mst/2-1.md` (which the owner has confirmed correct) carries
`"Setting Session Defaults" in topic 2.7` as plain text at line 59; hosted
serves it as `<a href="2.7?DT=...#HDRCTLSETD">`. That is the whole delta.

The four `SC09-2417-00` topics in #60's list that *do* lose structure
(`PREFACE.2.1`, `2.2.1.2`, `3.1.1.2`, `4.4.1.9`) are not C1 — their box art
sits inside a `cz OFF SYNTAX` region, which is C2.

## The `cz` dialect: one book of causes, not one cause

46 declining topics use the `cz` dialect, across six books —
`SC09-2417-00` (23), `SC41-485` (7), `GX27-3999-00` (7), `XWEBDEMO` (4),
`packet` (4), `SG24-204` (1). `prose_topic_cz.cpp` admits, in `handle()`:

- `OFF`: `table/etable`, `fig/efig`, `xmp/exmp`, `screen/escreen`,
  `lblbox/elblbox`, `fn`, `ent`, and the list closers `eul eol esl enotel
  edl eparml`;
- `FLOW`: `p pc gd pt`, `ul ol sl notel dl parml`, `li`, `dt`, `nt`, `note`,
  `fn`, `h2..h5`;
- `BREAK`.

Tags present in declining topics that are **not** in that set:

| tag | declining topics carrying it |
| --- | ---: |
| `OFF SYNTAX` / `ESYNTAX` | 11 |
| `OFF LINES` / `ELINES` | 4 |
| `OFF COVER` / `ECOVER` | 4 |
| `OFF ARTWORK` / `EARTWORK` | 3 |
| `OFF TIPAGE` / `ETIPAGE` | 2 |
| `OFF EHP0` | 2 |
| `OFF MSGL` / `EMSGL`, `FLOW MSGL/PROBD/ORESP` | 1 |

23 of the 46 carry an unmodelled tag; the other 23 fail on independent
`cz` admission rules or on non-`cz` checks. **The `FLOW` cluster is not one
family.** It is at least six:

1. `cz OFF SYNTAX` unmodelled (C2, 11 topics);
2. other `cz OFF` regions unmodelled (C3, 12 topics);
3. `cz` list/`dl` opener carrying text (C7, 6 topics);
4. `cz OFF LBLBOX` closure (C4, 6 topics);
5. compiled menu labels (C6, 2 of the 10);
6. the non-`cz` causes that happen to land in these books (figure region,
   second `ST`, malformed selector, control-like word).

### C2 — `cz OFF SYNTAX` is `XMP` under another name

Hosted `SC09-2417-00` `PREFACE.2.1` DT 19961114175628:

```html
<li>        Optional items appear below the main path.
</pre><pre width="80"><!-- * -->
       &gt;&gt;__<kbd>STATEMENT</kbd>__ _______________ ______________________&gt;&lt;
                      |_<var>optional_item</var>_|
</pre>
```

The region is a `<pre width="80">` exactly like `XMP`/`SCREEN`/`LBLBOX`, and
the surrounding topic *is* typed at hosted: `<ul>`, `<li>`, `<I>` heading run.
Across the 11 C2 topics hosted emits 9 `<ul>`, 42 `<li>`, 6 `<dl>`, 23 `<dt>`.

The decline message differs only by which check fires first: 7 topics reach
the `cz` block builder and say `cz off syntax carries display text`; 4 trip
the box-art check first and say `placeholder run '??'`. Same cause, split
across #60 and #63.

### C7 — a list opener may carry text

`SC09-2417-00` `3.1.2.2` record 361:

```
361  8  cz FLOW DL 3 3
361  9  cfont 3 6 2 13 3 2   Option    Tag        <- column header of the DL
361 10  cz FLOW DT 3 13
```

`handle()` rejects any text on a `FLOW <list>` opener. `XWEBDEMO 1.1` is the
same rule with a lead-in sentence (`cz FLOW UL 3 3   For the information
provider:`). Six topics; hosted emits 6 `<dl>` and **57 `<dt>`** for them.

`packet 4.5.1` is the same class of over-strict admission in a different
rule: `cz FLOW H5` is the trailing level announcement, and `handle()` allows
only `fn` directives after it — but this footnote body contains
`cz OFF XMP`/`cz OFF EXMP`, so the topic falls. Hosted serves 6 separate
`<pre width="80">` code blocks. **This is one of only two reproducible `cz`
gaps inside the redistributable book** (the other is `packet COVER`/`TITLE`,
C3), which matters for #59.

### C4 — `cz OFF LBLBOX` closure

Two shapes, both rejected by "the next directive must be `ELBLBOX`":

- `SC41-485` `1.2`–`1.6`: the labelled box contains a nested table/figure
  envelope (`SRFIGTBLUNIQ1`/`SRTBLTBLUNIQ1` … `SRETBL`/`SREFIG`/
  `cz OFF ETABLE`) before `cz OFF ELBLBOX`.
- `SG24-204` `NOTICES`: `cz OFF LBLBOX` is never closed — the topic ends
  inside the box, carrying two `cselect HDRNOTICES` cross-references.

`SC41-485 1.2` also ends `cemenu` / `cz OFF ENT` / `cz FLOW H3`, so it would
still have to clear the menu pass afterwards. `bootrace --coverage` reports
only the first decline, so this is an inference from the source, not an
observation.

## C6 — the compiled menu's labels are not bounded

Seven topics say `content follows the trailing menu` and three say
`trailing menu targets rejected`. The reported cause is a red herring: a
trailing `cz FLOW Hn` after `cemenu` is present in **68 typed topics of
`SC09-2417-00` alone** and is fine. What actually fails is a stray segment
*inside* the menu:

| book | topic | stray |
| --- | --- | --- |
| `SC09-2417-00` | `3.3` | `553:0` = `[` |
| `SC09-2417-00` | `4.3.6` | `1125:0` = `<<` |
| `SC31-711` | `BACK_1.12` | `534:0` = `/` |
| `SH12-565` | `2.1.2` | `77:0` = `'` |
| `SC24-5520-00` | `8.13.2` | `1799:10` = `SR Option` |
| `SC24-5527-02` | `6.3` | `592:3` = `SRVAPPS Tables` |
| `SC24-5527-02` | `6.4` | `610:8` = `SRVBLDS Table` |

The first four are one-token artefacts at a physical record boundary; the
last three are `cmitem` labels whose text carries an inline structural
control, so the label is split across segments. Hosted emits the
`Subtopics:` menu for all 10 topics.

`SC09-2417-00 3.3` (the owner's second example) also loses 3 `<a href>`
cross-references that hosted serves inside `<ul><li>`; its `FLOW UL/LI`
leakage is a symptom of the whole topic falling, not of lists being
unmodelled.

## C8 — one topic holds the corpus's largest link loss

`SH20-918` `INDEX` declines with

```
prose topic rejected: text segment begins with control-like word 'cidelm' in record 608
```

Hosted serves that page with **569 body `<a href>` links** — the entire
subject index of the book. It is 45% of all 1,258 links lost across the 274
declining topics, in one topic, for one unmodelled index control.

The rest of C8 is the mirror pair of segmentation faults:
`csourcefn carries visible payload 'X'` (11 topics) and
`text segment begins with control-like word 'X'` (12), plus the 14
`ACPZMST1` `SRHDR<name> carries visible payload 'ST'` topics + `ITPPIBOK E.0`
+ `SC26-457 2.0`, which are currently filed under #67 but are the same shape:
the segment boundary landed inside or across a control. **The class is 39
topics, not #64's 23.**

## C14 — five topics, 84 links, one empty index term

`SH12-565` `4.7.2`, `4.7.3`, `4.7.5.1`, `4.7.5.3`, `APPENDIX1.5.9.2` decline
on `SI control has an empty index term`. `SI` displays nothing (the verbatim
route already suppresses it). Hosted serves 84 body cross-references across
those five topics. Highest loss-per-topic of any cause outside `SH20-918
INDEX`.

## Blast radius

`RenderSeverity::typed_degraded` exists precisely for "typed lowering claimed
the topic, but one block could not prove its structure and was emitted
verbatim". It is used **11 times** corpus-wide. Meanwhile 274 topics fall
*entirely* to `best-effort` on a single unproven construct.

Three worked examples of the disproportion:

| topic | the one unmodelled thing | what falls with it |
| --- | --- | --- |
| `SC09-2417-00 PREFACE.2.1` | one `cz OFF SYNTAX` region | 3 `<ul>`, 20 `<li>`, 2 `<dt>` |
| `SC09-2417-00 3.3` | one stray `[` inside the menu | 1 `<ul>`, 3 `<li>`, 3 links, the note, the subtopics menu |
| `SG24-204 4.1.1` (#65) | one inline picture | 42 images and the whole topic |

Whether block-level degradation is safe here is a design question, not a
measurement, and is filed separately. It is *not* a licence to invent
structure: the degraded block still renders verbatim; only the proven blocks
around it become typed.

## Two things confirmed NOT defects

- **C1's verbatim rendering.** Hosted is verbatim for all 60. Reflowing or
  restructuring them would be a regression.
- Numbered steps written as body text stay paragraphs
  (`AnalysisNotes/numbered-paragraphs-are-correct-2026-08-30.md`).

## Issues filed from this analysis

| issue | cause | topics |
| --- | --- | ---: |
| #72 | verbatim topics carry no cross-references (1,258 hosted links) | 116 |
| #73 | `cz OFF SYNTAX` unmodelled | 11 |
| #74 | `cz OFF COVER/LINES/ARTWORK/TIPAGE/EHP0` unmodelled | 12 |
| #75 | `cz FLOW` admission too strict (list-opener text; footnote with a block) | 7 |
| #76 | `cz OFF LBLBOX` closure rule | 6 |
| #77 | compiled menu item labels not bounded | 10 |
| #78 | `SH20-918 INDEX` sentinel glyph — 569 links | 1 |
| #79 | `SI` term beginning with a control-shaped word — 84 links | 5 |
| #81 | whole-topic vs block-level fail-closed (blast radius) | — |

## Recommendation on the existing issues

- **#60 — close.** Its premise is false (all 64 declines are decoded box
  drawing, not undecodable bytes) and hosted renders 60 of the 64 verbatim
  too, so there is no structure to recover. The real loss is the 196
  cross-references, now #72. The other 4 topics belong to #73.
- **#63 — close or re-scope to an epic.** It is four independent causes:
  #73, #74, #75, #76.
- **#67 — re-scope.** Split out #77 (10 topics) and #78 and #79 (6 topics);
  move the 16 `carries visible payload 'ST'` topics into #64. Its description
  of the `SI` class is wrong — see #79.
- **#64 — leave open, but enlarge from 23 to 39 topics.** The 14 `ACPZMST1`
  `SRHDR<name> carries visible payload 'ST'` topics plus `ITPPIBOK E.0` and
  `SC26-457 2.0`, currently in #67, are the same segmentation fault.
- **#62 — leave alone.** 19 topics, a genuinely distinct IR-level cause;
  hosted shows 40 links and 2 headings lost. Nothing here contradicts it.
- **#61 — leave alone.** Confirmed coherent. `cz FLOW MSGL/PROBD/ORESP` in
  `GX27-3999-00 B.0` is the `cz`-dialect spelling of the same
  message-catalogue block, so `PROBD`/`ORESP` are not a separate family;
  they occur in one declining topic. 155 hosted links lost.
- **#65 — leave alone.** Confirmed by measurement: 83 of the 91 hosted
  `<img>` in declining topics are its 6 topics.
