# Corpus regression gate

`tools/corpus_gate.py` compares the current `bootrace --coverage` and
`--conservation` reports with an explicit, edition-pinned baseline. Both
reports cover the stable book topic catalog: every entry in the lightweight
topic index, followed by any TOC-only entries. Each row retains its source
catalog kind and index and its logical-record bounds, so an id that moves to
different source evidence is a regression rather than the same observation.
The gate uses only the Python standard library. Private books must be supplied
separately; none are distributed by this tooling. `packet.boo` retains its
normal CTest coverage.

```
python3 tools/corpus_gate.py --corpus /path/to/private/books --jobs 4
python3 tests/corpus_gate_synthetic.py
```

The corpus directory is flat. Both `.boo` and `.BOO` suffixes are included;
filenames are case-sensitive. Every listed book must be present and no extra
book is silently admitted. The baseline pins each file's byte size and SHA-256,
plus its document number, title, timestamp and reported version. A file identity
mismatch fails before decoding, even when the replacement has the same name or
document number. No book is downloaded automatically.

For a separately pinned set whose private files do not share a directory, pass
each path explicitly and select that set's baseline:

```
python3 tools/corpus_gate.py \
  --book /private/GA19-5486-13.boo \
  --book /private/geist-design-ez30ad01.boo \
  --book /private/geist-design-SC21-8295-03.boo \
  --book /private/geist-design-dfhp7a00.boo \
  --baseline /path/to/reviewed-supplemental-baseline.json --jobs 4
```

`--book` is repeatable and mutually exclusive with `--corpus`. The baseline
still identifies books by case-sensitive filename, so duplicate filenames are
rejected even when they come from different directories. Explicit inputs must
be regular `.boo` or `.BOO` files. This mode changes only how private files are
located; identity, edition, topic and conservation checks are identical.

The gate rejects a missing or unexpected catalog topic, a change to its source
identity, any per-topic severity
regression (including strict typed to typed-degraded), a reduction in conservation
checked-topic count, an increase in any topic's unframed-record count, or an
increase in any individual topic/word's unaccounted count. Recovering a word in
one topic cannot excuse losing a different word or regressing another topic's
route. Improvements are enumerated without changing the baseline. Unknown or
inconsistent report schemas fail instead of being interpreted as clean output.
At the same severity, changing the lowering family or degradation details also
requires explicit review rather than silently passing. A schema 3 baseline
also fingerprints every topic's rendered output (see below); a passing gate
still does not imply byte-identical output.

Conservation emits an identity row even for a clean or genuinely empty topic;
the gate requires its topic set and source identities to equal coverage exactly.
It skips records without proven display-line framing and forgives some
opening-word shortfalls that could be control opcodes. The gate tracks unframed
growth but does not prove such records faithful. It does not establish
reference-reader equivalence, visual layout fidelity, or absence of
duplicates/reordering. Those require targeted comparisons and regression
tests. In particular, a placeholder-run decline earns no exemption.

Emitted-word conservation checks Markdown and HTML independently. Each format
has its own per-topic/per-word loss ratchet and summary. Markdown
text is recovered from the serializer's complete byte-range trace, not by
guessing Markdown syntax; that trace must match the actual cached output.
HTML text projection preserves inline adjacency and block boundaries and
accounts for ordered-list markers and image replacement text. Link
destinations, unrelated attributes and diagnostic comments cannot supply
missing book words. The compatibility emitted count is the minimum across the
two formats, while the independent counts state which serializer lost text.
This remains an
occurrence-count check, not a proof of correct word order or geometry; exact
rendered-output comparisons are still required.
The text projections accept only the serializers' supported syntax and fail
on unsupported or malformed markup rather than guessing its visible text.

## Rendered fingerprints

Word conservation cannot see a bold run turn italic, emphasis slide onto the
wrong letters (`*o*rientation o`), a list lose its alphabetic markers, or an
anchor move. Baseline schema 3 records, per topic, compact fingerprints of the
rendered output. The gate obtains them from the same pass as conservation:
`bootrace --conservation --rendered` adds a `# rendered` row per topic with
the Markdown visible-text projection (from the serializer's byte-range trace,
so an escaped `\<name\>` is text, never a tag) and the HTML fragment. No
Markdown grammar is guessed; style and structure are read from the HTML,
whose markup is unambiguous, with Python's standard `html.parser`. Malformed
or unbalanced HTML fails the run.

| Category | Recorded | On change |
|---|---|---|
| characters | letter/digit multiset, case-sensitive, of the HTML text (text nodes and image alternatives) and, only where it differs, of the Markdown projection | any lost character in either format is a **regression**; a gain needs review |
| mid-word | count of style boundaries between two adjacent letters/digits in one block | any increase is a **regression**; a decrease is an improvement |
| style | hash of the ordered (style kind, covered text) runs, plus run counts per kind | needs review |
| structure | hash of the ordered block elements (tag, first `geist-` class, `type`, `value`, `start`, depth and empty-item attributes, nesting depth), plus counts per block kind | needs review |
| anchors | hash and count of `id` attributes, in order | needs review |
| links | hash and count of link destinations and image sources, in order | needs review |

A style run is a maximal stretch of one block's text under the same set of
open `em`, `strong`, `u`, `code` (or opaque inline code) elements; nested
`strong`/`em` is the kind `em+strong`. Blocks, `<br>` and images end a run and
a word. A link does not: `<em>a</em><a>b</a>` is a mid-word boundary.
Existing mid-word boundaries (for example `` `CLIST`s ``) are recorded, not
judged; only new ones fail.

The gate prints one `REVIEW` line per changed topic and category, with a
short summary such as `style: em 0->3, strong 3->0`, followed by a per-book
`CHANGED TOPICS` count per category. Review changes are not fatal by default;
`--fail-on-review` makes them regressions too. For an exact diff, keep the
capture's `--raw-dir`: it holds `<book>.fingerprint.json` with the full
ordered lists, and passing it back as `--baseline-raw DIR` adds the first
differing entries to each `REVIEW` line, for example
`{-'strong:K' +'em:K'}`. The fingerprint detail contains book text, so like
the other raw reports it stays outside the repository.

A book in which the decoder finds no topics at all is measured and pinned
like any other, but an unchanged empty book proves nothing about rendering,
so both `--capture` and a check print a `ZERO TOPICS <book>` line for it.

Rendering fingerprints adds no decoder pass: the conservation run already
renders both formats. The extra cost is the transfer and Python parsing of
the HTML: on the 33-book, 10,378-topic corpus at `--jobs 2` a check took
236 s without fingerprints and 254 s with them. The schema 3 snapshot is
about twice the size of the schema 2 one (10.7 MB against 5.1 MB), mostly
character multisets; the capture's raw fingerprint detail is about 16 MB.

At the first schema 3 capture (`2fa47926`) the corpus has 515 mid-word
boundaries in 173 topics, for example `` `CLIST`s `` in `SC09-138`, a bold
`APPCPM` followed by an italic `xx` in `N2AH1MST`, and a bold initial `O`
before `scar` in `QSYSNEWG`. They are recorded so that only new ones fail;
recording them does not establish that they are correct.

A schema 2 baseline remains valid: the gate checks it exactly as before,
without requesting or comparing fingerprints, and says so. Captures are
schema 4 (see below). Migrating is an ordinary reviewed capture: run the gate
against the schema 2 baseline, capture a schema 3 snapshot at the same
revision, and confirm its route and conservation fields match the old one
before replacing it.

## Punctuation deficits

Word conservation counts letters and digits only, so a lost comma, a colon
dropped from `macroinstructions:` or a glossary term printed without its
delimiter passes it. Baseline schema 4 adds, per topic, the punctuation units
the HTML does not carry. A unit is a conservation word with the mark glued to
its end (`, . ; : ! )`; `?` is left out because the drawn projection writes it
for every unprintable glyph). The drawn side counts only units inside a
display line, with the same forgiveness for a line's opening word as the word
check; the emitted side is the HTML fragment's visible text, because the
Markdown projection reads the `:` of a `**Note:**` label as syntax.

The gate obtains them from the same pass as the fingerprints:
`bootrace --conservation --rendered --punctuation` adds one
`# punctuation` row per topic and unit (topic, unit, drawn, emitted,
unaccounted). The snapshot stores `{topic: {unit: unaccounted}}` per book. As
for words there is no aggregate allowance: an increase in any topic's
unaccounted count for a unit is a **regression**, and a decrease is listed as
an improvement. `bootrace --conservation --punctuation` alone still prints the
standalone table with its evidence column for review.

A schema 3 baseline remains valid: the gate measures punctuation but does not
compare it, and says so in a `NOTE`. A schema 4 baseline without punctuation
data is rejected. Captures are always schema 4.

## Reviewing a baseline change

Checking never writes the baseline. An explicit capture creates a *new* file,
refuses to overwrite an existing file, and requires the source revision:

```
python3 tools/corpus_gate.py --corpus /path/to/private/books \
  --capture /tmp/corpus-candidate.json --revision COMMIT \
  --raw-dir /tmp/corpus-candidate-raw --jobs 4
```

`--raw-dir` must also be new; it preserves the exact reports for review. Keep it
outside the repository because reports contain source-text evidence. A capture
retains classification fields and loss counts, omitting unused decline prose
and source-line evidence from the maintained JSON snapshot. A capture
records observations, not approved exceptions. Compare its per-topic changes
with the maintained baseline and explain every accepted difference before
replacing the baseline in a scoped commit. Do not refresh the baseline merely
to make a failing change pass. Retain existing loss allowances until their
specific improvements are reviewed, then ratchet them downward. Adding an
edition requires a separate reviewed identity and measurements.

The older ratchet and conservation scripts were removed during the August 2026
cleanup. Their historical totals are not assumed by this gate; the maintained
snapshot records current measurements and the exact source revision that
produced them.

The initial snapshot was measured from revision `4470c8c9` using a frozen,
statically linked `bootrace`: 33 private books, 7,238 TOC topics, 7,103 strict
typed, two typed-degraded and 133 best-effort. It records 70 unaccounted word
occurrences and no unframed records; existing allowances are known findings,
not declarations of correct output. All 7,238 topics were checked. The corpus
contains seven uppercase `.BOO` filenames, so a lowercase-only sweep misses
part of it. For example, the full initial snapshot has 59 placeholder-run
declines; the 26 lowercase books alone have 50. Neither count establishes
faithfulness or authorizes an exemption.

The baseline was ratcheted at `d1b9bbf6` after the caption fix: the 20 lost
word occurrences in `SC09-2417-00` topic `1.3.3.4` are no longer allowed.
The remaining total is 50, with the same route counts and no new losses.
Markdown and HTML comparisons covered all 10,379 indexed topics across the
33 books, independently of the TOC-only diagnostic gate. The reviewed output
changes were three complete source titles, the restored caption, and 96
caption-ending dots proven to be structural line-length bytes rather than
visible punctuation.

The source-framing repairs at `674710fa` remove the remaining 50 allowed
occurrences. Independent checks of both serialized formats also exposed and
recovered seven previously masked word losses; they did not create new
allowances. Message-header metadata is no longer counted as drawn body text.
The reviewed snapshot has 7,106 strict typed topics, two typed-degraded and
130 best-effort, with all 7,238 TOC topics checked and no unframed records.
The three strict-route recoveries are `SC09-2417-00` `3.1.6.1`,
`SC33-033` `4.40`, and `SG24-204` `BACK_1.2`.

Review covered Markdown and HTML for 37 books and 11,568 indexed topics,
including four books outside the maintained gate. The indexed inventory lost
one false topic: an indented `SH19-6639` continuation now remains in
`SH12-565` `BIBLIOGRAPHY.2`. Native examples retain complete literal rows,
origins, spaces, blank lines, styles and link destinations; no table structure
is inferred from aligned font starts. Source-ordered picture figures and
source-attached glyphs retain their previous output in `GA19-5486-13`.

Twenty-four TOC publication topics change lowering family at the same strict
severity; two further indexed publication topics are outside the TOC gate.
Their publication titles now retain source-backed inline emphasis. The
text-only publication family declines native font spans it cannot represent;
default-font entries and undecided-framing compatibility remain supported.
These changes were reviewed as rendered output, not accepted by severity
counts alone. Synthetic tests cover the failure cases and mutations of the
row, ownership, font, link and serializer evidence.

The message-introduction repair at `7a78ca93` recovers four strict routes:
`SC24-546` `A.0`, and `SC34-425` `2.4.8`, `2.4.19`, `2.4.24`.
Complete native rows before a later compatibility font run retain their own
source claims; hidden index terms and native line-length bytes are not prose.
Blank native rows separate paragraphs, including across record boundaries.
The reviewed inventory is 7,110 strict typed, two typed-degraded and 126
best-effort topics, with all 7,238 TOC topics checked and no word-loss or
unframed-record allowances.

HTML and Markdown comparisons cover all 10,378 indexed topics in the 33
maintained books. Fourteen topics change: the four recovered topics and ten
already-typed `SC34-425` topics gain native paragraph boundaries and complete
metadata anchor names. The only removed word occurrences are the hidden
`c.sp 1 c` operands in the three recovered `SC34-425` topics; no drawn words,
hyperlinks or resource references disappear. Three partial `LEN` anchors
become the complete source-backed metadata names; no hyperlink in that book
targets the partial name. The `SPTREF92` destination
in `SC24-546` remains available, and unsupported font geometry still declines.
The promoted catalogue's non-ASCII text is UTF-8, with font columns mapped
independently of encoded byte lengths. Synthetic regressions cover these
cases and reject missing records, unproven framing and altered provenance.

The same repair leaves all output in the four additional books unchanged
relative to the preceding source-backed-heading fix (`34fb9720`), including
the ordered figures, mapped glyphs and earlier table repairs in
`GA19-5486-13`. These extra books add 1,190 indexed topics to the rendered
comparison; their coverage is not folded into the maintained 33-book floor.

The deferred-font and selector repair at `704fcc6d` recovers `SC34-425`
`2.4.9`, raising the floor to 7,111 strict typed, two typed-degraded and
125 best-effort topics. All 7,238 TOC topics remain checked, with zero
unaccounted words or unframed records. The font instruction at the end of
one record and its first native target row in the next retain separate
source claims. Every admitted font span is checked against that row's own
columns, independently of the message-entry origin. Missing records,
unproven first target rows, unsupported styles and invalid geometry decline.

Promotion initially exposed a lost cross-reference, so the repair also
models canonical, source-positioned anchor selectors inside message fields.
Each selector binds native source cells to projected UTF-8 byte offsets,
not to the first matching substring. Its control must occupy its own line;
all selected glyphs must survive projection. Unsupported targets, out-of-row
ranges and unrepresentable link/highlight overlaps decline rather than
silently discarding evidence. Synthetic cases cover repeated labels,
non-ASCII words, altered provenance and row-ending quotation marks.

The HTML and Markdown differential again covers 37 books / 11,568 indexed
topics. Exactly six topics change: `SC34-425` `2.4.9` becomes typed while
retaining its existing `HDRTELDB` link; `SC34-425` `2.4.17`, `2.4.31` and
`N2AH1MST` `2.0`, `3.0`, `18.0` gain seven source-proven links between them.
Their destinations are present in the rendered books. `2.4.31` also regains
the closing quote of a selector label that compatibility projection had
suppressed. The sole removed word occurrences are leaked `c.sp 1 c`
operands in `2.4.9`; no drawn words or existing hyperlinks disappear.
Its partial `LEN` metadata anchor becomes the complete source-backed name,
as with the preceding four promotions, and no existing book hyperlink
targets that partial name. The four additional books remain byte-identical.

The all-indexed-topic baseline at `f341e3b0` replaces the earlier TOC-only
ratchet. It records all 10,378 topic-index entries in the 33 maintained books:
10,216 are strict typed, two are typed-degraded, and 160 are best-effort. All
10,378 topics have matching coverage and conservation source identities and
none has an unframed record. Markdown and HTML are checked independently; each
currently retains one explicit loss allowance, the word `version` in
`IBMMMSTR` topic `1.5.332`. The independently reviewed GX27 alpha-list repair
retains the source `a.` and `b.` markers in both formats and therefore carries
no allowance in this baseline.

## Library sample (100 books)

Measured 2026-10-23 at `ee046824` (Release build, `-DGEIST_BUILD_GUI=OFF`)
outside the gate: an inventory of the wider private library, not a baseline.

**Method.** Python `random.Random(20261023).sample` over the sorted
8,236 `.boo`/`.BOO` files of the library, taking the first 100 whose name stem
or SHA-256 is not one of the 33 maintained or four additional books (none had
to be skipped). Per book, with hard timeouts and two books at a time:
`booinfo` (60 s), `bootrace --coverage` (300 s) and
`bootrace --conservation --rendered` (300 s); the reports were parsed with the
gate's own parsers and every topic's HTML fragment fingerprinted (which
rejects malformed HTML). The slowest single run took 26 s; the whole sample
877 s of tool time. Two further `booinfo` prefilters (400 random books, seed
20261024, and all 1,202 uppercase `.BOO` files) checked the patterns below.

**Headline.** All 100 books open; no run crashed, hung, exited non-zero or
wrote to stderr. One book yields no topics (below). The other 99 have 37,629
indexed topics: 30,775 strict typed (81.8%), 6 typed-degraded, 6,848
best-effort (all on the legacy route) and none failed. 16 books are entirely
typed. One book, `dfhcag49` (GC33-1694-25, 5,306 topics, 4.7 MB), is 98.9%
best-effort; without it the share is 1,598 of 32,323 topics (4.9%). All
topics were conservation-checked with no unframed record, every HTML fragment
parsed, and no topic rendered empty. Conservation reports 791 unaccounted word
occurrences in 301 topics of 30 books, identical in Markdown and HTML; 764 of
them are in 294 *typed* topics:

| Loss | Occurrences | Topics | Books |
|---|---|---|---|
| `PICTURE n` placeholder inside an artwork envelope; the image is emitted with its description as alternative text | 507 | 252 | 14 |
| selector or menu control operands (`cvselect`, `cmitem`) | 40 | 9 | 5 |
| other drawn text | 244 | 50 | 19 |

The first row is likely an accounting gap rather than lost text and needs
review. The third contains confirmed silent losses in typed topics, each
checked against the hosted BookServer: `SC33-1369-09` drops revision-barred
table body rows (`Y` cells, 159 occurrences in 23 topics, e.g. `2.1`);
`SH24-5219-00` `3.0` turns a 28-word paragraph into an index-term anchor
name; `ICHA6MST` `3.1.2` loses the routing-code numbers of a definition list
and bolds only their first letters (`**M** aster`). In `GA27-4120-00` the
hosted server also shows `1.` for a `Step 1.` label, so its `step` count is
not a loss there, but the typed output repeats the number (`1. **1.**`).

**Broken documents.** `EDCGV1R2.BOO` (GC09-1281-02, 90,112 bytes, header
version 1.0, timestamp `10/31/89 ????????`, no build version) opens but
produces no TOC entry and no topic; `bootrace --coverage` then also writes a
`# severity` line with no fields, which the gate's parser rejects. The hosted
BookServer serves its `COVER`. The uppercase prefilter found four more
pre-1.2 books, all with the same zero-topic result: `EDCIV1R2`, `EHMGMST`,
`EHMPMST` (1.0) and `EDCUSER` (1.1); every other book checked is 1.2. The
library also holds 73 zero-byte `.boo` files, which `booinfo` rejects as
smaller than one page; none was sampled.

**Patterns.** Degradation follows the CZ dialect (non-zero byte at directory
offset `0x0009`): 49 CZ books have 6,653 best-effort topics of 23,469
(28.3%; 7.7% without `dfhcag49`), the 50 others 195 of 14,160 (1.4%). By
`BUILD` property: none 2.3%, 1.3.0 30.5% (41 books), 1.3.1 5.5% (3 books).
By timestamp the share rises from 0.5% (1989-91) to 11.5% (1998-2000). Every
sampled book is English.

**Top decline classes** (numbers, quoted text and ids normalised; full
table, 134 classes, kept with the scratch reports):

| Decline | Topics | Books | Examples |
|---|---|---|---|
| display row precedes the first CZ directive | 2,920 (434 outside `dfhcag49`) | 16 | `SC31-8223-01` `1.1`, `GC30-3820-03` `2.4.1` |
| cz control is incomplete | 1,791 | 1 | `dfhcag49` `EDITION` |
| cz mode is not FLOW, OFF or BREAK (mostly a blank mode) | 771 (34) | 8 | `SG24-5314-00` `2.3.5.16`, `G544-3290-04` `GLOSSARY.2` |
| ST title does not match the topic title | 176 (23) | 5 | `SC41-5401-00` `2.1.2.5` |
| `c.sp` control carries visible payload | 145 | 3 | `GC41-5051-01` `1.0`, `ehxb7000` `1.2.1` |
| placeholder run `?` followed by visible text | 119 | 16 | `SC33-4034-16` `1.1.3`, `S85G-8681-00` `8.4.1` |
| cz OFF SYNTAX block has no display rows | 95 | 2 | `SC31-6552-00` `1.6` |
| font style code is not a highlight phrase (mostly `T`) | 66 | 6 | `GC23-0084-02` `5.4.1` |
| topic carries a second ST control | 58 | 2 | `SC31-8212-00` `2.3.3` |
| placeholder glyph `°` inside prose text | 55 | 16 | `SC33-1369-09` `PREFACE.2` |
| heading level `figlist`/`tlist` is not h1-h6 | 25 | 20 | `SC33-0668-02` `FIGURES` |

The hosted BookServer renders the first uppercase-named example of each
class shown (its `<base href>` matched the requested book and topic in every
case); `dfhcag49`, like other all-lowercase filenames, is not in the hosted
catalog. The six typed-degraded topics are four unmodelled `cz OFF`
regions, one inferred table column set and one figure-body anchor position.
