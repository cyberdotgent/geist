# Block-level degradation: whether to admit a topic with one unproven block

Design proposal for issue #81. Measured on `0ad3a73` (the tree after "Model a
picture placed inside a sentence"), not on the population #81 and
`AnalysisNotes/declining-topic-root-causes-2026-08-30.md` were written
against. That matters: the population moved under the issue twice while it was
open, and both times in the direction that weakens its case.

**Recommendation up front: write the policy now, build the mechanism later —
as one slice, after the family work settles.** The invariant below is worth
committing to today. The code is not, because every class it would apply to is
already an open modelling issue whose fix produces `typed`, which is strictly
better than `typed-degraded`, and because the cause fixes now landing are
outperforming degradation on every axis at once.

## Reproduction

```
cmake -S libgeist -B build_rls -DCMAKE_BUILD_TYPE=Release
cmake --build build_rls --target bootrace -j 8
for f in BOO/*.boo BOO/*.BOO libgeist/tests/fixtures/packet.boo; do
  build_rls/bootrace "$f" --coverage
done
```

`packet.boo` is no longer under `BOO/` (issue #59). A glob over `BOO/*.boo`
alone silently drops it and under-reports by four topics.

Hosted control: each declining topic fetched from `cbrdoc01`, body taken
between the first `<hr>` and the last `<hr>`, `Subtopics:` menu excluded and
counted separately.

Two details of that fetch are easy to get wrong, and getting either wrong
produces numbers that look plausible and are not. Both are stated here because
the first version of this note got both wrong.

**The shelf id and the edition must come from the book file, not the
filename.** `booinfo` prints them:

```
$ build_rls/booinfo BOO/SG24-204.boo
  Timestamp: 12/18/97 05:46:40
  Document number: SG24-2047-00
```

so the URL is `BOOKS/SG24-2047-00/4.1.1?DT=19971218054640`. Truncating the
filename to `SG24-204` does not fail — it silently resolves to a **different
book**, `SG24-2041-00`, and serves a real page for the same topic id. Half the
corpus has a filename that is not its document number (`SC24-546.boo` is
`SC24-5466-04`, `IEAC6MST.BOO` is `GC28-1631-2`). Where the document number is
not on the shelf, the filename stem is a working fallback.

**A content picture is `<img src=".../bookmgr/pictures/...">` wherever it
appears — including inside `<pre>`, which is where this reader puts them.**
Excluding images inside preformatted blocks, or treating every `/bookmgr/`
image as chrome, discards exactly the content pictures. Only the reader's own
chrome gifs (`/bookmgr/<name>.gif`, not under `pictures/`) are excluded, along
with the anchors that wrap them, `bookmgr.exe` service links, and the
`picture-N?mode=zoom` anchor the reader wraps around each content picture —
that is a zoom affordance, not a book cross-reference.

## Severity census on `0ad3a73`

| severity | topics |
| --- | ---: |
| `typed` | 7,135 |
| `typed-degraded` | 11 |
| `best-effort` (declining) | 216 |
| **total** | **7,362** |

Ratchet: 7,146 / 7,362.

## 1. The block-scoped split does not hold up. It is 50 / 166, not ~100 / ~90

The root-cause note split the then-274 declines roughly 100 block-scoped /
90 topic-scoped. Applying the test below to the current 216 gives:

| | topics |
| --- | ---: |
| **block-scoped** — a proven *block* boundary encloses the unproven thing | **50** |
| **topic-scoped** — no such boundary exists | **166** |

Two corrections account for the gap, and each is a real finding rather than a
counting difference.

### 1a. A proven boundary is not the same as a proven *block* boundary

The note counted C1 (60 topics of drawn box art on a display row that also
carries words) as block-scoped, on the grounds that the row is proven. It is:
`LayoutIR` proves every physical row. But a row inside a flowing paragraph is
not a block boundary. The paragraph's extent is decided by the *flow* — where
the prose stream starts and stops — not by any delimiter. Excising one row
from the middle of it splits a block whose extent the source never states, and
the two halves are then structure the source did not prove. That is a *larger*
claim than declining, not a smaller one.

Sixty-six topics fail this way today (62 box-art rows, plus four
`row columns are unproven`), and a further eighteen fail on a font/selector
span, which has no block boundary at all. All 84 are topic-scoped under the
test below.

This is the single largest correction and it cuts the same way the note's own
caveat did — it had already observed that hosted is verbatim for all of C1, so
"the gain is only the surrounding paragraphs".

### 1b. Eight declines are block-scoped that the message does not say are

The decline message names whichever check fired first, so it cannot be trusted
as a classifier on its own. Walking the segment dump of each of the 64
`placeholder run` declines and asking whether the named record lies inside a
matched delimiter pair finds eight that do:

```
SC09-2417-00 PREFACE.2.1  2.2.1.2  3.1.1.2  4.4.1.9   inside cz OFF SYNTAX .. cz OFF ESYNTAX
SC24-5527-02 3.8.4.6  3.10.4.4  3.14.1  7.4.1         inside SRTBL .. SRETBL
```

The four `SC09-2417-00` topics confirm the note's inference; the four
`SC24-5527-02` topics are new. `SC09-2417-00 PREFACE.2.1` carries seven
matched `cz OFF SYNTAX` / `cz OFF ESYNTAX` pairs and a modelled
`cz OFF XMP` / `cz OFF EXMP` pair; the offending token (record 29, token 177)
is inside the second `SYNTAX` region.

### The 50, by what proves their boundary

| boundary | topics | issue |
| --- | ---: | --- |
| `cz OFF <tag>` .. `cz OFF E<tag>` (SYNTAX, COVER, LINES, TIPAGE, ARTWORK, ELBLBOX) | 23 | #73, #74 |
| `SRTBL` .. `SRETBL` | 10 | table envelope |
| `cz FLOW <list>` .. `cz OFF E<list>` | 8 | #75 |
| `CMENU` .. `CEMENU` | 7 | #77 |
| `SRFIG` .. `SREFIG` | 2 | #65 |

`SRFIG` was 5 on the previous tree. `0ad3a73` took `SG24-204` `4.1.1`,
`4.2.1` and `5.1` out of this set by modelling the inline picture — see §7.

### The 166, by why no boundary exists

| reason | topics |
| --- | ---: |
| row-scoped only: box art on a flowing row | 66 |
| no closed region: `SRMSG` opens nothing that closes | 30 |
| frame unproven: topic `ST` envelope | 22 |
| row/inline-scoped only: font or selector span | 18 |
| frame unproven: ownership ledger has an unclaimed cell | 9 |
| boundary unproven: the region is never closed | 9 |
| no region at all: a bare inline control (`SI`, `SRLIS`) | 8 |
| frame unproven: segmentation | 6 |
| boundary unproven: the extent is exactly what failed (implicit grid, misaligned list) | 5 |
| nothing to keep | 1 |

Note the nine "never closed". They are the test working: `cz OFF LBLBOX is not
closed by cz OFF ELBLBOX` (6), `table envelope is not closed by SRETBL` (2),
`figure region has no picture selector (unterminated)` (1). The same families
appear on both sides of the split, separated by whether the source closed the
region. Nothing else in the taxonomy separates them.

## 2. The invariant

A family may admit a topic containing a degraded block only when all six hold.
The first three are what replaces "the whole topic verified"; the last three
are what pays for it.

1. **Frame proven, unchanged.** The topic's metadata envelope, segmentation,
   layout ledger and `VerifiedOwnershipIR` all verify exactly as they must
   today. Degradation is reachable only *after* the frame is proven; a family
   that has not proven its frame has no coordinates in which to name a region,
   and must still decline the topic whole.

2. **Boundary proven independently of the failing check.** The region's first
   and last source positions are named by a matched pair of source controls
   that the frame already recognised — `cz OFF X` / `cz OFF EX`, `SRFIG` /
   `SREFIG`, `SRTBL` / `SRETBL`, `CMENU` / `CEMENU`, a message section
   envelope. If the check that failed *is* the closure check, the boundary is
   not proven and the topic falls whole.

3. **The boundary is a block boundary.** The extents of the blocks before and
   after the region are fixed by the delimiters, not by the region's content.
   A merely-proven boundary is not enough (§1a): a display row inside a
   flowing paragraph is proven and is still not admissible, because excising
   it would split a block whose extent the source does not state.

4. **Total region conservation.** The degraded block claims *every* source
   cell between the delimiters — visible and invisible alike, with no
   exception. It is the maximal claimer over `[begin, end]`; no other block
   claims inside it and it claims nothing outside. This is stronger than the
   rule a typed region obeys today (`prose_topic_spans.cpp`: an unclaimed
   token is admitted only when it carries no visible word), and deliberately
   so — the whole point is that the block asserts nothing about what is inside,
   so it must own all of it.

5. **Verbatim means verbatim.** The region lowers to a `PreformattedBlockIR`
   whose lines are the region's own display rows in source order with per-line
   origins, and to nothing else. No inline model, no links, no emphasis, no
   ordinal, no nesting. The block asserts the rows exist in that order and
   nothing more.

6. **Marked, at the block.** `fidelity = degraded`, a stable
   `degradation_code` naming the unmodelled construct, and a
   `degradation_detail` naming the check that failed — *and* a marker in the
   rendered file at the block itself, which does not exist today (§4).

## 3. The verifiers already express this, and nothing has to be weakened

Checked directly.

**`verify_document_ir` is fidelity-agnostic.** It never reads
`DocumentNodeOriginIR::fidelity`. Its checks are structural and per-node:
topic identity complete, record range not reversed, at least one block, and
for each node a valid derivation, ordered non-duplicated slices, ordered
non-duplicated rows, and — the load-bearing one — a `decoded` node must name
a source slice. A degraded `PreformattedBlockIR` satisfies all of these by
construction, and invariant 4 makes it satisfy the last one maximally rather
than minimally. **No relaxation is required to admit a degraded block, and
none should be made.** This is pinned by a new assertion (§6).

**Ownership does not have to change either, and there is a precedent for the
shape.** `OwnershipRunConflictIR` already carries exactly this idea one layer
down: a run whose ownership conflicts is recorded as *unowned* while the
ledger as a whole stays verifiable, and consumers must decline any structure
that would include it —

> Run-scoped, typed conflicts. The ledger remains verifiable; the listed runs
> are simply unowned.

That is run-scoped fail-closed inside a verified ledger, which is
structurally the same move as block-scoped fail-closed inside a verified
topic. `VerifiedOwnershipIR`'s private constructor is untouched by any of
this: a degraded block is a decision made *downstream* of a successfully
built ledger, by invariant 1. Nothing here makes an unverified ledger
representable, and nothing should.

**The conservation verifiers are the part that needs care, and invariant 4 is
the answer.** Today a family that cannot prove a region's content declines,
so the region's cells never reach a ledger. If it instead emits them, they
must be owned. Making the degraded block the maximal claimer over the whole
delimited extent is the cleanest available statement: the region is owned
exactly once, by a block that claims nothing about it.

## 4. What the reader sees — and the gap that must close first

`render_diagnostic_comment` already emits, at the top of the topic's Markdown:

```
<!-- geist-render: severity=typed-degraded route=typed family=prose
     reason=degraded-block records=28-32 degraded=cz-off-region-unmodelled
     detail="cz OFF SYNTAX is not modelled; ..." -->
```

That is honest and sufficient for one degraded block. It is **not** sufficient
for the general mechanism, because `document_markdown_renderer.cpp` never
reads `fidelity`: a `PreformattedBlockIR` is rendered by `append_fenced_block`
whatever its provenance. So an *unproven* verbatim region and a *proven* one
are byte-identical in the file.

This is not hypothetical, and the figure family shows why it matters. Its
drawn body is verbatim and deliberately **not** degraded:

> Clean, not degraded: an ASCII/CFONT-drawn figure *is* character art the
> compiler rasterized at build time, and hosted BookServer reproduces it line
> for line inside `<pre>`. Keeping the display rows verbatim equals the
> reference renderer, so nothing about the source is lost and no structure is
> being claimed. Degradation is reserved for real loss.

A topic can therefore contain a proven verbatim figure body *and* an unproven
verbatim region, rendered identically, with one topic-level `degraded=` code
that does not say which is which. **Block-level marking is a prerequisite, not
a follow-up.** The proposal is a comment immediately before the fence naming
the code, e.g. `<!-- geist-block: degraded=cz-off-region-unmodelled -->`,
emitted only for `fidelity == degraded` so every existing typed output stays
byte-identical.

### The existing 11 are a narrower claim than #81 proposes

Worth stating plainly, because #81 rests on "the mechanism exists and is
under-used". Of the 11 `typed-degraded` topics on this tree, ten are
`figure-body-cross-reference` or `figure-body-anchor-position` and one is
`message-preformatted-fallback`.

The ten figure ones are **named, enumerable loss inside a block whose
structure is proven**: the block is verbatim by right, and a specific link or
anchor position could not be expressed in the target format. The degradation
detail names exactly what was dropped.

What #81 proposes is different: **unproven structure inside a proven
boundary**. The doc comment on `RenderSeverity::typed_degraded` describes the
second, but only `message-preformatted-fallback` (`SC31-711 5.0`) actually
does it — the section's provenance is incomplete, so the block is not eligible
for semantic promotion and goes out verbatim inside an otherwise typed topic.

So the mechanism is not "used 11 times and under-used". It is used once in the
sense #81 means. That does not sink the proposal, but it does mean the
generalisation is being argued from one instance, not eleven, and the risk
assessment should say so.

## 5. The measurable prize

Hosted body counts over the 216 declining topics (214 fetched; `SC09-138 H.0`
and `N2AH1MST 28.0` are not on the shelf).

| | topics | `<ul>` | `<dl>` | `<dt>` | `<li>` | pictures | body links |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| block-scoped | 50 | 17 | 15 | 107 | 91 | 5 | **61** |
| topic-scoped | 164 | 8 | 0 | 0 | 25 | 3 | **1,015** |
| **total** | **214** | 25 | 15 | 107 | 116 | 8 | **1,076** |

Block-scoped, by boundary:

| boundary | topics | `<ul>` | `<dl>` | `<dt>` | `<li>` | pic | links |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `cz OFF <tag>` .. `cz OFF E<tag>` | 23 | 13 | 6 | 23 | 58 | 3 | 8 |
| `SRTBL` .. `SRETBL` | 10 | 2 | 0 | 0 | 22 | 1 | 40 |
| `cz FLOW <list>` .. `cz OFF E<list>` | 8 | 2 | 9 | 84 | 11 | 0 | 10 |
| `CMENU` .. `CEMENU` | 7 | 0 | 0 | 0 | 0 | 0 | 2 |
| `SRFIG` .. `SREFIG` | 2 | 0 | 0 | 0 | 0 | 1 | 1 |

Plus: ten of the 50 carry a hosted `Subtopics:` menu, 208 entries in total.
And moving 50 topics from `best-effort` to `typed-degraded` moves the ratchet
7,146 → 7,196 (97.1% → 97.7%).

Read this honestly:

- **Definition lists are the real prize, and degradation is the only route to
  them.** All 15 `<dl>` and all 107 `<dt>` lost corpus-wide are in
  block-scoped topics — and all of them are in `SC09-2417-00`. The
  topic-scoped set has none. Two-thirds of them are in the eight
  `cz FLOW <list>` topics alone, which is #75.
- **Cross-references are not the prize.** Block degradation recovers **61 of
  1,076 body links, 5.7%**. 566 of the remaining 1,015 — 53% of everything
  lost — are in `SH20-918 INDEX` alone, which is topic-scoped and needs #78's
  model; the next largest single topic-scoped loss is `SC34-425
  APPENDIX1.5.3` at 115.
- **Pictures are no longer the prize either, but only because they were
  fixed.** Eight content pictures remain across all 216 declining topics, five
  of them block-scoped. That is the state *after* `0ad3a73`; see §7, because
  how they stopped being the prize is the argument.

### Correction: the earlier picture measurement in this note was wrong

The first version of this note stated that hosted places zero content images
in any declining topic, and that #81's "42 images" for `SG24-204 4.1.1` was
reader chrome. **Both statements were false**, from the two method faults now
documented under Reproduction: the fetch resolved `SG24-204` to a different
book, and the image filter discarded `/bookmgr/pictures/` images along with
the chrome gifs. Fetching `BOOKS/SG24-2047-00/4.1.1?DT=19971218054640` and
counting `<img src>` under `/bookmgr/pictures/` regardless of enclosing
element gives:

| topic | hosted content pictures |
| --- | ---: |
| `SG24-204 4.1.1` | 42 |
| `SG24-204 4.2.1` | 27 |
| `SG24-204 5.1` | 13 |
| **total** | **82** |

#81's figure of 42 for `4.1.1` was exactly right, and the pictures sit
*inside* the `<pre>` — which is why a filter that skips preformatted content
misses all of them. The corresponding claim in
`declining-topic-root-causes-2026-08-30.md` ("91 hosted body `<img>`, of which
83 are C10") was therefore closer to the truth than this note's first attempt
at correcting it, and the note's own C10 attribution was sound.

## 6. Testing the fail-closed argument — it does not hold as stated

#81's central argument is that declining a whole topic over one bad block
"over-claims in the other direction": it asserts the other 95% is unprovable
when it was proven.

**As stated, this is false.** Fail-closed is a rule about what the rendered
artefact *claims*. A verbatim topic claims exactly one thing — "these are the
source's display rows, in source order" — and that claim is true; the repo
owner has confirmed verbatim output is faithful. It asserts nothing about
provability. The severity ladder and the `detail` field say precisely which
check failed and where, so nothing about the other 95% is being asserted
either way. There is no over-claim to correct.

**The weaker version does hold, and is the real case.** Declining discards
facts the pipeline had already proven. That is a loss of *information*, not a
false claim. So block degradation is not a fail-closed correction that comes
for free — it is a **recall improvement under an unchanged fail-closed rule**,
and it has to be justified by what it recovers and paid for with a stronger
invariant, because it widens the surface on which a wrong boundary could
silently produce wrong structure. Section 5 is that justification, and it is
thin.

There are now two clean counter-examples to the "you must claim more to
recover more" intuition. `612db5f` (ending a control's opcode at its own
display line) recovered 55 topics while *tightening* what the code claims, and
`0ad3a73` (modelling a picture placed inside a sentence) recovered three more
with 82 pictures, in resource id, order and alt text. Recovering structure by
proving more is available, and it is dominating.

## 7. What the evidence says about cause fixes versus degradation

The coordinator's question: is the residue irreducible, or does it yield to
family work? Measured, not assumed.

Two cause fixes landed while this issue was open. Classifying each population
by the same test:

| | 274 (as filed) | 219 (`612db5f`) | 216 (`0ad3a73`) |
| --- | ---: | ---: | ---: |
| block-scoped | 56 | 53 | **50** |
| topic-scoped | 218 | 166 | **166** |

`612db5f` took **52 topic-scoped** declines — the population block degradation
**cannot** address — and only 3 block-scoped ones. Within the topic-scoped set
segmentation went 40 → 6 and display-line length byte 19 → 0.

`0ad3a73` then did the thing that settles the argument: it took **3
block-scoped** declines — from degradation's own target set, the `SRFIG`
class, which dropped 5 → 2 — and delivered them as **`typed`**, with 82 hosted
pictures reproduced in resource id, order and alt text. Degradation would have
delivered those same three topics as `typed-degraded` with the region verbatim
and **all 82 pictures still lost**, because a degraded block asserts nothing
and therefore carries no image references. The model fix did not merely beat
degradation on severity; on this class it recovered content that degradation
structurally cannot.

That also disposes of #81's most striking exhibit. `SG24-204 4.1.1` was its
worst case — "one inline picture takes 42 images and the whole topic with it".
The 42 was real (§5). It was recovered by modelling the picture, not by
degrading the region around it.

All three of #81's worked examples have now been answered: `3.3` by `612db5f`,
`4.1.1` by `0ad3a73`, and `PREFACE.2.1` by the one-line `cz OFF SYNTAX` model
below. None of the three needed block degradation.

And the block-scoped 50 are not a residue either. Every one of them maps to an
open modelling issue, and for the largest class the model is nearly free:
`prose_topic_cz.cpp` already handles `xmp`, `screen` and `lblbox` through one
generic path — require a matched `cz OFF E<tag>`, require display rows, emit
preformatted, then treat the closer's trailing text as paragraphs. Hosted
serves a `cz OFF SYNTAX` region as `<pre width="80">`, verified directly:

```html
</pre><pre width="80"><!-- * -->
       &gt;&gt;__<kbd>STATEMENT</kbd>__ ____________ ______________&gt;&lt;
                      |_<var>optional_item</var>_|
</pre>
```

— which is exactly what that path already produces for `XMP`. Adding `syntax`
to that tag list yields **`typed`**, not `typed-degraded`.

**Degrading those 23 topics would be effort spent reaching a strictly worse
outcome, and this is the clearest case in the whole argument.** The same
region, in the same file, is served two ways depending on which route reaches
it: `typed` if `syntax` is in the tag list, `typed-degraded` if it is not and
the region is degraded instead. The verbatim rows are identical either way —
the region is a `<pre>` in both. What differs is only what the topic *claims*,
and the model fix claims the truth (this is a verbatim region, proven, exactly
as `XMP` is) where degradation claims ignorance about a region the source
delimits as plainly as the one next to it. This belongs in **#73**.

**The evidence says the residue is not irreducible.** Seven agents are on
#61, #65, #69, #72, #73–#76, #77–#80; those issues cover all five block-scoped
classes and the largest topic-scoped ones. Block degradation is complexity
bought for a remainder that is currently shrinking faster than the mechanism
could be built, and whose largest component (`<dl>`/`<dt>` in
`SC09-2417-00`) is #75's target anyway.

## 8. Recommendation

**Do not build it now. Do these three things instead.**

1. **Adopt the invariant in §2 as written policy** — this is #81's first
   acceptance criterion and it is worth having whether or not the mechanism is
   ever built. It is the rule that decides block-scoped from topic-scoped, and
   it is what makes the nine "never closed" declines correctly stay whole.

2. **Close the two prerequisites, which are safe to land independently of any
   family.**
   - *Block-level marking* (§4). Until an unproven verbatim region is
     distinguishable from a proven one in the file, the mechanism cannot be
     used honestly at scale.
   - *A strict-`typed` ratchet floor.* `tools/typed_route_ratchet.py` reads
     `# summary typed=`, which counts `typed` **and** `typed-degraded`
     together (7,146 = 7,135 + 11). A family could today convert a proven
     topic into a degraded one and the ratchet would not notice. `bootrace`
     already prints the split on its `# severity` line and
     `inventory.by_severity` already carries it, so this is parsing, not new
     measurement. Two monotone floors — strict `typed` may not fall while
     `typed + typed-degraded` rises — is #81's fourth acceptance criterion and
     the guard that must exist *before* degradation becomes general.

3. **Revisit after #61, #65, #72–#80 land**, and re-run §1's classification
   then. If a class survives with a proven block boundary and no plausible
   model — a genuinely unmodellable construct — degrade it then, as one slice,
   under the §2 invariant. Judging by §7, that residue will be small and may
   be empty.

**Where degradation would be right if it were needed today**, the cleanest
target is the generic `cz OFF` path in `prose_topic_cz.cpp`: an unmodelled
`cz OFF <tag>` that *is* closed by a matched `cz OFF E<tag>` and whose rows
are preformattable already proves everything invariants 1–5 require, and the
code currently reaches `return fail(error, "CZ layout " + name + " is not
modelled")` at exactly the point where it could instead emit the same
preformatted block marked degraded. That is the one-slice shape, and
`packet.boo` carries three instances of it (`COVER`, `TITLE`, `4.5.1`), so it
is testable under #59 without any unpublishable book. It is deliberately not
done here: #73–#76 are being fixed by other agents right now, and their fixes
produce `typed` for these same topics.

## What this note changes in the code

One test function, `degraded_block_topic()` in
`libgeist/tests/render_diagnostic.cpp`, and nothing else. It is a synthetic
pin on the mechanism, not on any family, and it exists because §3 and §4 rest
on three claims about the verification and diagnostic layers that ought to be
assertions rather than prose:

- `verify_document_ir` accepts a document containing a degraded block with no
  relaxation — fidelity is orthogonal to verification;
- one degraded block, and only a degraded block, moves a topic from `typed` to
  `typed-degraded`, and the block's own reason code reaches the reader through
  `render_diagnostic_comment`;
- degradation is unreachable when the frame is unproven — a declined lowering
  is `best-effort` and carries no degradations at all (invariant 1).

It also pins the §4 gap: today `render_document_markdown` of a document with a
degraded block is byte-identical to the same document without it. That
assertion is written to *fail* when the recommended block-level marking lands,
which is the point of it.

It further restores coverage lost with the unpublishable books: the header of
that test records that the `typed-degraded` pin used to stand on `SC31-711
5.0` and went away with #59.

`ctest -j6 -LE slow` 25/25, `ctest -j3 -L slow` 5/5,
`typed_route_ratchet.py` 7,146 / 7,362 unchanged.

## Method caveats

- The block-scoped / topic-scoped classification is applied to the decline
  *message*, which names whichever check fired first. §1b corrects the one
  cause where that is demonstrably misleading (`placeholder run`, checked
  against the segment dump for all 64). Other causes could hide the same
  thing; the 50 is therefore a lower bound on block-scoped, and the direction
  of any error strengthens rather than weakens §7's conclusion only if the
  extra topics are *not* already covered by an open modelling issue.
- The delimiter-nesting walk in §1b tracks `cz OFF X`/`EX`, `SRFIG`/`SREFIG`
  and `SRTBL`/`SRETBL` over the segment dump. It is a heuristic over rendered
  text, not the family's own parser, and could mis-nest a pathological topic.
- Hosted counts depend on the shelf-id, edition and chrome rules stated under
  Reproduction. Getting the first two wrong is silent — a truncated book id
  serves a real page from the wrong book — so any future hosted measurement
  should take the id and `DT` from `booinfo` and spot-check one known page
  before trusting an aggregate. This note's own first-pass numbers failed
  exactly there.
- Content-picture counts must not exclude images inside `<pre>`; this reader
  places them there. See the correction in §5.
