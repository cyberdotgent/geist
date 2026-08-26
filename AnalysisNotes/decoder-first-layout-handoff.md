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
