# Decoder-first layout reconstruction handoff

## Status

This note records the stopping point for rendering issue #48 after commit
`d2f3111`.  All experimental publication and CMITEM implementations described
below were rejected and reverted.  The repository was clean before this note
was added.

Issue #48 is not one remaining string-cleanup bug.  It groups several cases in
which physical display structure was flattened before semantic ownership was
known: publication catalogs, menu titles, glossary rows, message action lists,
and fixed prose continuations.  Renderer-side recognizers repeatedly repaired
the SC31-711 examples while corrupting structurally similar content in other
books.  Further work should therefore start in the decoder/provenance layer.

## Evidence and rejected approaches

The corpus contains 4,940 logical records whose CFONT spans are all `C`, and
825 records with a coarse row-shaped signature.  These include publications,
message catalogs, tables, forms, source listings, surveys, and ordinary prose.
CFONT style and row-like whitespace are not semantic ownership.

The corpus also contains 9,963 CMITEM segments.  Of these, 3,499 end in a
one-byte encoded token spanning 261 decoded values.  Many are genuine title
content, including `S`, `E`, `W`, numbers, `Command`, `Guide`, `Reference`, and
balanced punctuation.  A compact terminal token is not necessarily a marker.

One SC31 compact identity decoded as `can` occurs 212 times, but only four
occurrences immediately precede an exact three-column physical-row origin.
Compact dictionary identity can prove that a value is marker-capable; it does
not prove that a particular occurrence is structural.

Rejected, uncommitted approaches included:

- all-C or row-shaped publication classification;
- publication grouping based on capitalization, punctuation, document-number
  patterns, or visible continuation text;
- topic-local or book-wide removal of marker-capable compact identities;
- literal cleanup of the observed `<`, `can`, `/`, `bridge`, or `address`
  strings;
- terminal CMITEM cleanup based only on encoded width and menu position.

The publication prototypes recovered some missing rows but still merged the
two independent ANSI citations in `BACK_1.7`, merged distinct X/Motif entries,
or omitted markerless first rows.  The CMITEM prototype either hard-coded the
three observed values or, after removing those gates, produced no supported
visible change.  None was committed.

## Information currently lost by the rendering pipeline

The decoder retains decoded token words, encoded value/width, assembled word
origins, and token output spans.  The authoritative rendering path still
projects these into flattened logical-record strings too early.  It does not
carry the following facts as typed data:

- control kind and exact operand-token versus payload-token ranges;
- markerless control-payload row starts;
- complete physical-row token and output spans;
- native display origin/columns;
- explicit marker-slot provenance;
- hard object/paragraph boundaries versus soft physical wraps;
- stable display-run identity across immediately adjacent logical records;
- semantic ownership by table, form, publication, menu, message, glossary,
  procedure, figure, or ordinary prose;
- a conservation partition classifying every source cell as control operand,
  padding/origin, marker slot, or visible payload.

Some of these facts are directly encoded.  Others, especially hard versus soft
breaks, are state derived by the IBM reader.  Geist must reproduce that layout
state during decoding instead of inferring it later from rendered text.

## Intended decoder primitive

Add a decoder-owned representation along these lines, in focused source files
rather than expanding `toc.cpp` or `markup.cpp`:

```text
TypedSourceSegment
  logical_record
  segment/control kind
  source token begin/end
  operand token begin/end
  payload token begin/end
  assembled output begin/end

SourcePhysicalRow
  stable display run id
  logical_record and segment identity
  row token begin/end
  visible output begin/end
  native origin column
  row start = control_payload | explicit_slot | placeholder_wrap
  optional marker token/value/width
  break before = hard_object | hard_paragraph | soft_wrap
  continues_previous_record
```

Extraction must happen while decoding still knows token/control boundaries.
The existing flattened decoded string should remain a compatibility projection,
not the authoritative source for physical layout.

The first workload should add representation, extraction, and tests with zero
render changes.  Required cases are:

- a digit-leading CFONT payload that must not be parsed as another operand;
- the markerless first row in SC31-711 logical record 537;
- multiple explicit rows in one CFONT segment, including multi-character
  one-byte slot tokens;
- question-run/padding soft wraps;
- the SC31-711 logical-record 519 to 520 continuation;
- intervening-control and non-adjacent-record continuation negatives;
- two-byte marker lookalikes, combined marker/origin tokens, malformed source
  vectors, and out-of-range spans;
- exact partition/conservation: no source payload gap, overlap, duplication, or
  unclassified visible token.

## Migration order

After the primitive is independently verified:

1. Build a typed, all-or-nothing publication catalog composer.  It should map
   one source display run to one hosted paragraph and reject any catalog whose
   visible source cells are not conserved exactly.
2. Migrate CMENU/CMITEM boundary handling.  The record-boundary orphan between
   `BACK_1.12.1` and `BACK_1.12.2` should be classified by run state, not by its
   decoded slash.
3. Migrate glossary introductory rows and cross-record continuations.
4. Migrate message Action/Meaning lists and the remaining fixed-prose runs.
5. Migrate issue #51's ordinary and message-local procedure/list families onto
   the same typed row/run ownership model.

Each migration slice requires focused acceptance tests, a complete CTest run,
an affected-book BookServer audit, and a whole-corpus before/after differential.
Book tracker #27 must remain open until #48 and #51 are complete and a fresh
82-topic SC31-711 verification pass finds no new rendering issue.

## M6 CMENU/CMITEM migration evidence

The typed menu slice was validated on 2026-08-26 against baseline commit
`0f84268`.  Both baseline and candidate rendered all 34 repository books with
`boo2git`, producing 7,396 Markdown files.  The final differential contained
22 changed topics in four books: 16 in `SC09-2417-00`, four in `SC31-711`, one
in `SC26-457`, and one in `IBMMMSTR`.  Every delta removed only a verified
origin-free terminal menu-boundary token; all other 7,374 Markdown files and
all generated resources were byte-identical.

An earlier candidate produced 173 changed topics in nine books.  The corpus
gate correctly rejected it: a one-byte dictionary token can expand to a URL,
a long fill run, or meaningful punctuation.  The final admission rule also
requires compact marker-slot geometry, no independent row origin, and an
exact canonical target-title match after removing that one source token.
Regression tests retain `XWEBDEMO`'s URL and period suffixes and DREICMST's
row-origin-owned period.

All 22 changed topics were fetched through the hosted BookServer reader.  The
31 affected link labels matched the candidate output after ordinary whitespace
collapse.  Sources used:

- `SC31-7111-00`, topics `2.4`, `4.1`, `BACK_1.12`, and `BACK_1`, timestamp
  `19941010174546`;
- `SC09-2417-00`, topics `2.1`, `2.1.3`, `2.2.4`, `2.3.6`, `2.3.12`, `3.1`,
  `3.1.1`, `3.1.4`, `3.2.2`, `3.3`, `3.3.1`, `3.5.5`, `4.3.6`, `4.4.1`,
  `4.4.2`, and `4.5`, timestamp `19961114175628`;
- `SC26-4570-01`, topic `3.7.3`, timestamp `19911220191142`;
- `IBMMMSTR` / document `SC26-4309-2`, topic `1.10`, timestamp
  `19911004151140`.

The repeatable URL form is
`http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/<book>/<topic>?DT=<timestamp>`.
The BookServer comparison must preserve source capitalization; the canonical
TOC title is an admission oracle, not replacement presentation text.

## M6 glossary-introduction migration evidence

The residual audit isolated `SC31-711.boo` topic `GLOSSARY`, logical records
435--437. The old renderer flattened the complete introduction into one
paragraph, leaked compact row slots such as `application`, `a`, and `(`, and
lost real carried words such as `be`, `are`, `for`, and `not`. It also joined
five citations and six cross-reference explanations. The `SRGLS` definition
tail already retained its anchors and fixed rows, so the migration is bounded
at the first `SRGLS` rather than rewriting the dictionary.

The new semantic projection is admitted only after verified Layout IR and
Ownership IR identify one glossary heading, one split title/lead row, five
source paragraphs, one split cross-reference lead, six explanation paragraphs,
and the first-term boundary. Every paragraph retains display-run and
physical-row provenance. Canonical re-lowering rejects modified text or
provenance. Rejected shapes remain on the compatibility path.

The local comparison source is `SC31-7111-00`, topic `GLOSSARY`, hosted
timestamp `19941010174546`:

`http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/SC31-711/GLOSSARY?DT=19941010174546`

The hosted page establishes the five citation boundaries, six emphasized
cross-reference labels, and the first glossary-term boundary. `bootrace --ir`
provides the repeatable local trace for logical records 435--437, including
token ordinals, encoded marker values, absolute BOO byte ranges, native
origins, run/row identity, and the admitted semantic projection.

The whole-corpus comparison used detached baseline `bdeee9a`. Both baseline
and candidate rendered all 34 books, producing 7,396 Markdown files and 7,885
total files. Exactly one file changed: `SC31-711.boo` topic `GLOSSARY`. Every
other Markdown file and generated resource was byte-identical. The complete
diff only separates the five citations and six cross-reference explanations,
restores carried `be`, `are`, `for`, and `not`, and removes the source-proven
layout slots and duplicate terminal punctuation described above.

## M6 numeric-message section migration evidence

The next residual was `SC31-711.boo` topic `5.0`, logical records 172--434.
The compatibility renderer retained all 396 `MSG` anchors but flattened each
message heading, `Meaning:`, and `Action:` into one paragraph. It also emitted
only 584 of the 792 section labels with emphasis. The hosted BookServer page
renders all 396 entries with separate, emphasized Meaning and Action sections.

The new `MessageCatalogIR` admits only a wholly numeric/range `SRMSG` catalog.
It records the canonical message ID and the ordered Meaning/Action boundaries
with display-run, physical-row, logical-record, and control-segment
provenance. Canonical re-lowering rejects mutated IDs, ordering, or
provenance. Split `CFONT` controls are explicit recovered-record-continuation
boundaries: the source text must begin with the missing label before the next
numeric `SRMSG`; arbitrary missing labels are not inferred. Lower-case words
carried in an `SRMSG` payload remain owned by the preceding entry.

The comparison source is document `SC31-7111-00`, topic `5.0`, hosted
timestamp `19941010174546`:

`http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/SC31-711/5.0?DT=19941010174546`

The whole-corpus comparison used the post-glossary baseline. Candidate and
baseline both rendered all 34 books, producing 7,396 Markdown files and 7,885
total files. Exactly one file changed: `SC31-711.boo` topic `5.0`. Every other
Markdown file and generated resource was byte-identical. The candidate has
exactly 396 Markdown paragraphs beginning `**Meaning:**` and 396 beginning
`**Action:**`; the remaining message text and anchors are conserved.

## M6 selector-control IR groundwork

Selector decoding now has a canonical `SelectorCatalogIR` between control
segmentation and the existing fixed-row compatibility projection. Every typed
`CSELECT` control is retained in source order. Canonical controls record the
absolute column, length, target, payload, table state, decoded UTF-8 byte
ranges, source-token ordinals, original BOO byte ranges, and any source-proven
one-cell marker/origin slot. A syntactically non-canonical control remains in
the catalog with an explicit rejection reason instead of disappearing or
being guessed.

Control-segment ranges were clarified as decoded UTF-8 byte coordinates. A
single checked conversion maps those ranges into assembled-word coordinates
before token ownership is consulted. The synthetic admission tests include a
non-ASCII prefix before `CSELECT`, signed/invalid numeric operands, mismatched
decoded operands, record/source cardinality mismatch, and mutated BOO-byte
provenance. Canonical re-lowering rejects every mutation.

The compatibility marker cleanup now consumes only verified selector slots and
requires exact column, length, and target identity in the normalized stream.
Presentation-family exclusions and table ownership remain explicit consumer
policy; they are not encoded as parser facts. `SC09-138.boo` topic `4.8`
also proves why rejected controls must remain typed: logical record 611 has a
bare `CSELECT` followed by display-ruler text, alongside valid selectors that
must still project. Topic `H.0`, logical record 2075, decodes table end as
`SRETBL,`; recognizing the attached comma restores the verified table-state
boundary.

This workload deliberately does **not** claim complete selector-backed
fixed-row lowering. Deferred/cross-record display payloads, selected display
cells, and row grouping are the remaining semantic layer. The control/marker
IR is the verified input to that work, and its compatibility projection must
remain a zero-Markdown-delta refactor.

The final whole-corpus `boo2git` comparison used the post-message baseline.
Both sides rendered all 34 books, 7,396 Markdown files, and 7,885 total files;
every exported file was byte-identical.

That byte identity is a migration gate for this compatibility-preserving M6
workload, not the acceptance definition for the future IR-native Markdown
renderer. Once rendering is driven directly from the semantic document IR,
intentional byte differences are expected. Acceptance then moves to valid and
stable Markdown structure, conserved source content and links, semantic
invariants, fixture-specific correctness (including issue #48), and reviewed
corpus deltas against BookServer evidence where available.

### Selector display binding core

The next selector layer is an output-neutral `SelectorDisplayIR`. It assigns
every canonical selector exactly one disposition: a span in one physical
display row, a resource object (`PIC<n>` or deferred `LNK`), or explicit table
ownership. Display rows preserve individual native cells and their logical
record, token, word, and BOO-byte provenance, plus the owning Layout IR run and
row. Equal adjacent targets remain separate selector spans; target equality is
not evidence that two selectors form one link.

Binding is an ordered, fail-closed operation. Inline payloads, later rows in
the same logical record, rows in the immediately following record, and
multiple selectors queued for one row are distinguished explicitly. Typed
object barriers, non-adjacent records, overlapping or out-of-range geometry,
and unresolved selectors reject the semantic lowering. The canonical verifier
re-extracts the structure and proves that all raw selectors have exactly one
binding and that every source display cell agrees with Ownership IR.

Focused fixture checks admit the selector rows in `packet.boo` topic `5.1.2`,
`QSYSNEWG.BOO` topic `PREFACE`, and `GG24-4302-00.boo` topic `NOTICES`, and
retain the `GG24-395-0.BOO` picture selector as table-owned. SC31 continuation
rows originally rejected where punctuation split one native line into several
Layout IR fragments. The display composer now rejoins only token-contiguous
fragments from the same run, record, and segment, restoring the exact owned
width-1 punctuation marker plus source-owned margin/padding cells. This admits
the two independent SC31 topic `5.0` selectors without merging their identical
targets: the first selects `Chapter 2, "Problem` at columns 56--75 across
logical records 172--173, and the next remains a separate inline selector.

Generated `FIGURES`/`TABLES` topics are admitted only under the exact
`CHDLEVEL :FIGLIST`/`:TLIST` plus matching `ST Figures`/`ST Tables` envelope.
Each selector owns one hard-boundary display row. Coordinate margins may be
restored synthetically, but visible span content must remain source-backed;
unlaid adjacent-record cells are first classified as opaque by Ownership IR
and then conserved with exact token/word/byte provenance.

The cross-record audit found an important boundary in
`SC24-5527-02.boo:TABLES`: record 23 begins with the exact decoded source
`4-2.  VMSES/E Build Lists   4.1.2`, but Layout IR begins its physical row at
the `.` marker. The preceding `4-2` cells therefore remain opaque rather than
disappearing. Selector lowering now carries those exact segment-prefix cells
into the row before native-margin alignment. It never fabricates a list label
or appends spaces merely to satisfy selector geometry.

Generated-list marker disposition is likewise source-driven: all-question
markers and `|` are structural boundaries, while `.`, `)`, `E`, `VM`, `CMS`,
`Service`, and other verified compact expansions are lexical cells restored
with their source-derived separators. The corpus inventory admits 28 generated
topics across 18 BOO fixtures, comprising 1,196 selectors, bindings, and hard
rows. Focused assertions cover the exact SC24 cross-record label, SC09 lexical
parenthesis continuation, and the independent SC31 native-coordinate path.
This semantic slice remains output-neutral.

## M6 comments/back-matter Layout IR prerequisite

The comments-form audit exposed a mechanical ownership error before semantic
questionnaire lowering could be safe. `SC31-711.boo` topic `COMMENTS` closes
its second table and figure in logical record 544, but `SREFIG` itself owns the
visible payload `Specific Comments or Problems:` and the first ruled fill row.
Records 545--546 continue that form. The previous Layout IR discarded the
end-control payload and then joined those records backward to the last table
`CFONT` run across `SRETBL` and `SREFIG`.

Layout extraction now admits visible payloads on the observed structural end
controls (`SREFIG` and typed `SRETBL`) as markerless, hard-object rows. A plain
record may continue that new run only when the prior payload-owning segment is
the final typed segment of the adjacent record. The verifier independently
rejects forged continuation across an intervening control or from an
ineligible run such as `ST`.

### Comment and delivery semantic core

`CommentDeliveryIR` now gives the two verified SC31 back-matter structures an
output-neutral semantic boundary. `BACK_2` is divided into a title-page block
and delivery-instruction block. `COMMENTS` is divided into its title block,
two balanced questionnaire-table objects, and the response area beginning in
the visible `SREFIG` payload at logical record 544 and continuing through 546.
Every retained line carries its Layout run/row, logical record, segment,
token range, native origin, break kind, continuation state, and complete
optional marker provenance.

Admission is structural rather than topic- or prompt-text based. It requires
the verified topic/source envelope, exact balanced object ordering, canonical
run kinds and row geometry, complete row assignment, and ownership of every
visible source cell. A corpus inventory finds 17 `COMMENTS`/`BACK_2`
candidates; only `SC31-711.boo` `BACK_2` and `COMMENTS` satisfy this bounded
shape, while 15 related and unrelated cross-book forms reject explicitly.
The canonical structure is visible in `bootrace --ir`, but renderer projection
remains separate so this semantic-core workload cannot change Markdown.

Ownership alone was not yet enough to lower these forms safely. Several
compact row starters are genuine missing words (`the`, `to`, and `or` in
`BACK_2`, and `information` in `COMMENTS`), while similar starters such as
`adapter`, punctuation, and `<` are layout artifacts. The enriched IR records
that disposition explicitly and admits a lexical starter only in the exact
conserved form geometry with alphabetic width-1 source provenance.

Each physical line is also subdivided into maximal source fields using only
Ownership IR `layout_padding` cells, never searches in flattened text. This
separates the combined Internet/checklist line in `BACK_2`, the mailing and
address transitions in `COMMENTS`, and isolated table-decoration fields.
Every visible source cell must belong to exactly one ordered field; decoration
is conserved but marked as suppressible. Document lowering can now compose
semantic content without rediscovering columns, losing lexical starters, or
turning structural borders into prose.

The first direct typed-render trial exposed a second ownership class which the
visible-field ledger did not cover: printable marker, opaque, and layout-padding
cells adjacent to fields. These cells contain real sentence/label fragments in
the admitted forms. `BACK_2` has ten such affixes (three compact lexical
prefixes plus sentence periods and delivery-label colons); `COMMENTS` has eight
(periods, `with`, the questionnaire `?`, an opaque label colon, and the compact
`information` prefix). Exact fixture assertions retain their logical-record,
token, BOO-byte, and decoded-word ranges.

Production classification does not key on those fixture coordinates. Inside
the already verified form envelope it uses source ownership and adjacency:
terminal punctuation attaches to the preceding field, lowercase marker text
may prefix the next field only across an unterminated sentence, and alphabetic
opaque text carries an explicit separating-space policy. Each admitted fragment
records prefix/suffix attachment, spacing, token/word range, and byte range.
Every other printable marker/opaque/padding fragment is retained in an explicit
structural-suppression ledger. Canonical verification requires every printable
non-field cell to have exactly one semantic or structural owner, so a newly
decoded cell cannot silently disappear.

The resulting trace ends the table in run 9 and starts run 11 at record 544
segment 2; records 545 and 546 continue run 11. Synthetic tests cover visible
`SREFIG`/`SRETBL` payloads, an empty end-control barrier, ownership
conservation, and forged crossings. The compatibility renderer is unchanged.
The 34-book `boo2git` comparison produced 7,885 files with zero byte
differences from commit `5a5cc81`.

## M6 fixed-prose continuation migration

The bounded `ST` title/body continuation path now lowers through canonical
`FixedProseIR` instead of rediscovering marker/origin geometry in flattened
decoded strings. The IR records the owning record and segment, exact decoded
title/body/boundary ranges, canonical title and paragraph, Layout run/row
identity, marker and three-cell origin token ordinals, encoded widths/values,
original BOO byte ranges, and the exact compatibility projection ranges.

Admission requires verified Layout and Ownership IR, exactly one nonmalformed
`ST`, one unique wide title/body boundary, at least two independently owned
width-1 marker plus width-1 three-space origin rows, no placeholder/question
display frame, and no printable opaque payload. Canonical re-lowering rejects
semantic or provenance mutations. The compatibility wrapper applies only the
verified source-coordinate edits; it no longer searches decoded text for
semantic row ownership.

The coordinate contract is explicit here as well: control ranges are decoded
UTF-8 byte offsets, while token ownership is indexed by assembled words. Both
directions are converted before ranges are compared or projected. The
synthetic positive contains a non-ASCII title before every body edit, and the
existing two-byte marker/origin, combined-token, drifting-origin, single-row,
semantic-frame, and multiple-`ST` negatives remain fail-closed.

The source audit admits exactly the established 11 candidates:
`GG24-4302-00.boo` internal `SH3.2.11.1` at logical records 177--178 (not a
TOC-exported topic) and topic `5.4.2`; `ITPPIBOK.BOO` topics `2.1.2`,
`2.4.2.2`, `3.0`, `4.1.2`, and `PREFACE.2`; and `SC31-711.boo` topics
`PREFACE`, `3.0`, `4.2.1`, and `4.3.3`. No other source region enters this
recognizer. The final 34-book `boo2git` gate produced 7,396 Markdown files and
7,885 total files, all byte-identical to the post-selector/layout baseline.

## M7 Document IR boundary

The document layer is intentionally smaller than the decoder and semantic
layers below it. It owns output-neutral topic identity, block/inline nodes,
and source provenance, but it must not include decoder control, Layout IR,
Ownership IR, or family-specific recognizer headers. Lowering adapters may
depend on both sides; dependency direction is always semantic IR to Document
IR, never back into the decoder.

The initial migration boundary uses one temporary `LegacyGmlRegion` for an
entire normalized topic. Its state scope is explicitly whole-topic: splitting
legacy records into independent regions would reset the existing GML state
machine and silently change list, table, example, or highlighting behavior.
This adapter is migration debt rather than a permanent opaque document node.
Verified semantic families replace portions or whole topics with typed nodes
as their lowering becomes complete.

The permanent vocabulary covers headings, paragraphs, anchors, lists,
definition lists, tables, preformatted blocks, notes, publication lists,
figures, footnotes, and index groups, with text, emphasis, code,
cross-reference, image, hard-break, and explicitly opaque inline leaves.
Every node carries a derivation and ordered source slices; canonical
verification rejects empty identity, invalid slice/token/byte ranges,
out-of-order provenance, malformed links/tables, and mixed or empty legacy
state scopes. A stable diagnostic formatter makes the boundary observable
before any renderer migration.

M7 begins with the whole-topic legacy adapter because it is state-safe and
behavior-preserving. M8 then introduces the pure Document-IR-to-Markdown
renderer and direct semantic lowerings. At that point acceptance is correct,
stable Markdown structure and conserved content/link semantics with reviewed
intentional corpus deltas; reproducing the legacy renderer byte for byte is
explicitly not the goal.

The production `TocEntry::markdown()` handoff was audited before and after the
whole-topic adapter across all 34 BOO fixtures. Both exports contained 7,396
Markdown files and 7,885 total files; recursive comparison reported zero byte
differences. This establishes that Document IR now owns the production topic
render boundary without splitting legacy renderer state. It does not certify
future typed Markdown byte parity, which is intentionally not required.

## M8 typed Markdown renderer

The Markdown renderer consumes only a verified `DocumentIR`. It has no access
to decoder tokens, physical rows, Layout IR, Ownership IR, or semantic-family
recognizers. Every block and inline variant has a deterministic rendering
policy, including context-specific escaping, variable-length code fences,
rectangular pipe tables, normalized link destinations, and explicit output for
opaque nodes. A sole whole-topic `LegacyGmlRegion` still delegates once to the
old state machine during migration; typed and legacy regions cannot be mixed.

Renderer validation is therefore performed on the emitted Markdown and its
structure, not by comparing it with the tokenized BOO parser. Decoder and
lowering tests prove source ownership and semantic conservation; renderer tests
separately prove stable Markdown syntax and representation of every IR variant.
Compatibility-only handoffs retain byte-identity gates, while later direct
semantic handoffs may produce reviewed, intentional byte differences.

Cross-references retain output-neutral target identity in Document IR. Each
target is explicitly a topic, anchor, resource, or external destination; a
Markdown exporter may supply a resolver that maps those identities to its
actual filenames and resource paths. This mapping cannot be guessed by the
generic renderer because `boo2git` also owns filename sanitization, collision
suffixes, README special cases, and cross-topic anchor/resource maps. Without
an exporter resolver, rendering uses a stable identity-preserving fallback
rather than inventing a `.md` filename in semantic lowering.

Fixed prose now has a separate whole-topic envelope above the earlier inner
`FixedProseIR` recognizer. It requires one logical record containing the exact
ordered topic metadata family, an optional source anchor immediately before one
terminal `ST` segment, and no additional control or visible content. It retains
the complete record range and segment ledger plus distinct heading, paragraph,
and anchor provenance. Corpus inventory admits only `ITPPIBOK.BOO` topics
`2.1.2` and `4.1.2`; nine other inner-`ST` shapes are rejected because they also
contain selectors, CFONT data, menus, messages, or trailing text. Canonical
lowering emits a heading, optional anchor, and paragraph. Production routing
admits those same two complete topics as the dispatcher's third mutually
exclusive family. Their only corpus differences are CommonMark escapes in topic
numbers, parentheses, and terminal periods; removing those escapes makes both
files byte-identical to the legacy visible text, while the source anchor and
paragraph boundaries remain unchanged. Partial fixed-prose shapes continue
through the indivisible legacy path.

The numeric-message semantic core is now enclosed by a complete, output-neutral
`MessageTopicIR` for `SC31-711.boo` topic `5.0`. It covers logical records
`[172,435)`, the ordered topic metadata and two-row chapter heading, all
introduction rows, 398 source anchors, two typed `anchor:HDRPROBS` selectors,
and the complete 396-entry `MessageCatalogIR` from `023` through `2505`.
Its source ledger has one item for each of the 2,279 decoded segments and
retains 4,698 physical rows with ownership cells; the final `2505` Action
(`None.`) is an explicit terminal boundary. Truncated, metadata-less, and
glossary envelopes reject, and the corpus admission scan accepts only this one
topic. Its introduction is now semantic rather than an untyped row list: exact
source-cell ranges form five paragraphs, including the one intra-row boundary
proven by a 20-cell layout-padding run after terminal punctuation. Two adjacent
selector atoms retain their independent source intervals and typed
`anchor:HDRPROBS` targets; selected cells replace prose cells exactly once and
are never rediscovered by label-string search. Marker carry, row breaks,
padding, and selector replacement are assigned before atom text is composed.
Mutations to paragraph order, padding boundaries, lexical carry, selector
targets/labels/ranges, anchor order, or cell claims reject. No production
Document-IR route is enabled yet; canonical whole-topic lowering and a separate
Markdown corpus audit remain the next slice.

The complete `SC31-711.boo` glossary envelope is now represented and lowers
canonically to output-neutral Document IR. `GlossaryCatalogIR` covers logical
records `[435,518)`, all 281 term entries, 21 alphabet sections, the terminal
`SRGLS`, raw term carry, physical definition rows, marker slots, exact source
slices/ownership cells, and the full segment ledger. Its explicit 302-item
sequence preserves the source interleaving of sections and terms. Definition
prose is assembled only from preassembled token spans whose exact ownership is
`visible_content`; marker, origin, and padding spans are omitted by disposition,
never by character value. A focused check proves a lexical `What?` survives
while a separate padding `???` token does not. The one embedded figure/table
envelope is semantic: LR454's four nested controls and five physical rows
produce a verified two-column grid with one header and six body rows, including
source-proven lexical carry for `1-15`. Lowering emits 21 section headings, 281
term anchors and definition entries, and one 7-by-2 table. The table owns its
five physical source rows exactly once and those rows are disjoint from the
definition prose. Production routing remains disabled until this complete
Document IR is audited as its own Markdown migration slice.

Generated figure/table lists now have a strict whole-topic model and canonical
Document-IR lowering. It composes the verified selector catalog and display-row
ledger with the topic metadata, `CHDLEVEL`/`ST` heading, every entry cell, and
all envelope segment slices. Each selector becomes one paragraph containing a
typed cross-reference with its raw BOO anchor identity; output paths remain a
renderer/exporter concern. Typed control ranges now distinguish the observed
three-operand `c.sp` spacing form and generic `cz` layout directives; only the
generated-list pass interprets the verified `BREAK`/`OFF` operands. Unknown
forms remain malformed and opaque. An all-topic corpus scan consequently admits
all 28 complete `FIGURES`/`TABLES` topics across 18 fixtures, conserving all
1,196 selector rows with no false positives or rejected generated-list topics.

Menu provenance is now explicit down to decoded output cell, token/word,
inserted-space kind, word value, and token byte range. A strict `MenuTopicIR`
wraps that core with metadata, optional anchor, title, menu boundaries, and
typed raw topic targets. Source-only extraction finds 153 structurally complete
menu envelopes, but structure alone cannot prove that a raw target names a book
topic or that its label is canonical. A separate `MenuTargetValidationIR`
records that book-level semantic evidence; `MenuTopicIR` requires it before
promoting a target to a typed topic reference. The existing catalog validation
admits exactly six complete topics:
`SC34-425.boo` topics `1.8.5.5`, `1.8.15.5`, and `1.8.18.5`;
`FA1PLMM0.boo` `5.6`; `SC33-033.boo` `5.3`; and `SH12-565.boo`
`APPENDIX1.9.5`. Each raw label independently equals its catalog title and none
requires terminal-marker repair. `BookTopicCatalogIR` now replaces the untyped
title map for this validation. It preserves TopicInfo header evidence and every
TOC projection, including logical-record spans and source-order indexes; exact
token/byte provenance is unavailable at that public boundary and is not
invented. Header titles are authoritative when present and TOC evidence remains
explicit corroboration. Catalog-only validation retains the same six-topic
boundary across all 153 raw envelopes. Those six topics now also lower
canonically to output-neutral Document IR: the source title becomes a heading,
the optional source anchor remains an anchor block, and all items form one
unordered list whose sole inline per item is a typed topic cross-reference.
Labels and raw target identities are copied unchanged, while heading and link
origins retain canonical token/byte slices from the exact title, target, and
label cells. Target, label, and provenance mutations reject. Production still
needs to build and share this catalog once with the whole-topic dispatcher;
widening based on raw shape alone remains unsound.

The publication-catalog adapter is the second complete semantic-to-document
lowering and is now enabled in production. `PublicationCatalogIR` retains
physical source rows for its title and introduction as well as every
entry paragraph; when a wide first row contains both title and introduction,
both semantic fields retain that shared row. The canonical source verifier
rejects missing or changed heading provenance. Document lowering emits one
source-proven heading, the introduction paragraph, and every independent
publication paragraph without forcing the data into `PublicationListBlockIR`:
the current source IR does not distinguish a citation title from a body, so
doing so would duplicate or invent content.

Production typed routing now occurs before `attach_topic_data()` can project
semantic IR back into flattened GML. A source-only dispatcher builds and
verifies Layout/Ownership once, independently recognizes comment/delivery and
publication-catalog models, and requires exactly one complete family to match.
It verifies the selected canonical DocumentIR lowering and returns no document
for every non-match or rejection. `TocEntry::markdown()` then selects exactly
one of two indivisible paths: the immutable cached typed document, or the
unchanged whole-topic legacy loader. Typed and legacy blocks are never mixed.

TOC entries and direct/non-TOC topic rendering install the same paired lazy
loaders over one shared decode state. A null typed attempt is cached as well as
a successful document, `gml_records()` remains available and unchanged before
or after typed rendering, and repeated rendering is stable. The public topic
ID is a synthesized prefix on the source-proven typed heading, preserving the
existing navigation identity without pretending the ID came from the comment
form cells.

`BACK_2` and `COMMENTS` are the first production typed topics. Their output is
intentionally judged as rendered Markdown: for example `BACK_2` is serialized
as `BACK\_2` and publication/FAX hyphens may be escaped, while their rendered
visible text remains exact. Evidence-backed physical continuations become
prose paragraphs, the two delivery reminders become a Markdown list, and the
questionnaire retains both source-derived legacy table anchors for each table.
Acceptance checks therefore normalize Markdown escapes instead of treating
legacy byte spelling as content conservation.

The production corpus gate exported all 34 fixtures and the same 7,396
Markdown / 7,885 total files as the M7 baseline. Recursive comparison changed
only `SC31-711.boo` `back_2.md` and `comments.md`; every other Markdown file,
README, and resource was byte-identical. Both intentional deltas were reviewed
for heading identity, paragraph/list/table structure, all 18 recovered affixes,
publication and FAX data, checklist items, questionnaire questions, response
area, and table-anchor conservation.

The publication production gate admits exactly 18 complete topics across three
books: four topics in `GG24-395.boo` (`3.3.9.4`, `3.3.10.6`, `3.3.14.4`, and
`3.3.18.4`), `BIBLIOGRAPHY.1` in `SC09-138.boo`, and 13 `BACK_1` catalog
topics in `SC31-711.boo` (`BACK_1.1`, `.2`, `.4` through `.11`, and `.12.1`
through `.12.3`). The fresh 34-book export still contains 7,396 Markdown files
and 7,885 total files. Relative to the M7 baseline it has 20 intentional deltas:
those 18 publication topics plus the two established comment topics. Removing
only CommonMark backslash escapes makes every publication output byte-identical
to its baseline, including heading level, paragraph boundaries and order, and
trailing separators. Tests therefore assert Markdown-visible publication text
while retaining strict occurrence counts and paragraph-structure checks; legacy
Markdown byte parity is neither required nor restored.

## M9 heuristic-removal audit

Typed Markdown bypass does not by itself make the legacy transformations dead:
`TocEntry::gml_records()` remains public and the existing migration tests
explicitly require its output to remain available before and after typed
rendering. The first safe cleanup is therefore the flattened-string admission
prefilter in `document.cpp`. When a document loader is invoked, it can decode
the typed source once and let the canonical family extractors fail closed;
string searches should not decide whether the typed dispatcher sees a topic.
That slice should also merge `fixed_layout_sources` and `typed_sources` into one
lazy source cache, with decode-count, GML immutability, exact family inventory,
full CTest, and corpus gates.

Publication all-C font admission is not dead code. `all_c_font()` remains an
active recognizer condition in `publication_ir.cpp`, so retiring all-C semantic
inference requires a source-envelope/ownership recognizer refactor and a new
negative test; deleting the existing condition would silently widen admission.
Likewise, publication title/string repairs in `toc.cpp`, menu and message
post-render projectors in `markup.cpp`, generated-selector compatibility
cleanup, and the global legacy Markdown state machine remain observable on raw
or untyped paths. They must be removed only in separate slices after branch-hit
audits show they are unreachable or after the corresponding public raw-GML
policy is intentionally changed. The planned order is: typed-source prefilter
and cache consolidation; publication all-C refactor; unreachable publication
repair deletion; menu/message projector removal after production migration;
and only then any global legacy-adapter retirement.

## Source trail

- Fixture: `BOO/SC31-711.boo`
- Document: `SC31-7111-00`
- BookServer book: `SC31-711`
- BookServer timestamp: `19941010174546`
- Hosted URL pattern:
  `http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/SC31-711/<topic>?DT=19941010174546`
- Last fresh verification in this session: 82/82 hosted and local topics,
  37 heuristic-flagged topics, with #57's five intended changes verified.
- Full #57 regression comparison: 7,396 Markdown files across all fixtures.
- Issue #48 exact publication source family: logical records 519-537;
  `BACK_1.12` records 533-534 are the immediate menu-only negative.

The BookServer CGI should be fetched through the Docker fetch MCP when direct
network access is redirected.  `build/bootrace` provides the repeatable local
logical-record/segment trace used for the record evidence above.
