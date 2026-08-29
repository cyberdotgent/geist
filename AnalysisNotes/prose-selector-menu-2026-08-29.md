# Prose selectors and trailing menu labels (issue #58)

Two prose-family rejection buckets, both diagnosed from positioned evidence
and lowered typed.  Format facts live in `Format/markup.md`
("CMITEM record terminator tokens", "LNK selector alternatives",
"Selector kind words as row-control slots"); this note records the workflow,
the hosted sources, and what stays fail-closed.

## Bucket A — "selector targets a picture or external link"

`prose_topic_stream.cpp` rejected every `CSELECT` whose operand target was
`PIC<n>` or the literal `LNK`.  A census over the 205 rejected topics
(936 selectors, `bootrace <book> <topic> --segments` on each) showed what the
bucket actually is:

| Selector shape | Topics | Books |
| --- | ---: | --- |
| `LNK <BOOK>` cross-book contents link | 155 | ITPPIBOK 84, SC24-546 18, SH12-565 15, SC24-5527-02 15, SC28-1881-05 13, SC41-485 8, SC31-605 1, GG24-395 1 |
| `LNK <HDR>` cross-book heading link | 5 | SC41-485 |
| `LNK <OTHER>` external URL | 4 | XWEBDEMO |
| `LNK <IMAGE>` inline external image | 1 | XWEBDEMO 1.4.1 (inside its `SRFIG` region, so the figure block owns it) |
| `PIC<n>` | 1 | ITPPIBOK 5.2.4 (owned by an anchorless figure span) |

So the bucket is overwhelmingly cross-book library references, not pictures.
`libgeist/src/selector_link_ir.cpp` parses the alternative list; the stream
pass gives the alternative tokens the control role and pushes only the row
text, so the existing deferred-selector column/length machinery places the
link exactly as it does for internal anchors.  Destinations follow hosted:
`DOCNUM/<docnum>/CCONTENTS`, `DOCNUM/<docnum>/HDR<anchor>`, or the URL.

Two shared rules the bucket forced out:

* A `CFONT` phrase fully inside a `CSELECT` phrase is the link's own label
  (hosted `<a ...><cite>TPNS</cite> <cite>General</cite>
  <cite>Utilities</cite></a>`).  `DocumentIR` has no nested inline, so the
  reference wins and the style is dropped; a partial overlap still rejects.
* The selector kind word (`<INTERNET>`, `<OTHER>`, `<IMAGE>`) is the display
  row's control slot when it stands before any row is open, and is padding
  when a second control-byte token (the row's usual marker glyph) follows it.

Still fail-closed: `PIC<n>` in prose that no figure span owns, an unmodelled
`LNK` kind, and an `<IMAGE>` selector outside a figure region.  None of the
three fires anywhere in the corpus.

## Bucket B — "raw menu label differs from canonical catalog title"

`validate_source_menu_targets` compares the raw `CMITEM` label with the
canonical catalog title, and `build_menu`'s fallback re-checks every item
against the TOC title.  The fallback always reported the *first* pass's error,
so the census had to be taken by instrumenting the fallback itself.  Over the
221 rejected topics the differences were:

| Class | Items | Cause |
| --- | ---: | --- |
| TOC title is a strict prefix of the label | 133 | the label ends with the record terminator `.` token |
| label equals the header title exactly | 24 | the fallback required `header.size() > candidate.size()` |
| other | 64 | the catalog's header title is a different kind of corruption |

The terminator is typed in `MenuIR` as `MenuTerminatorTokenIR` (the item's
last payload token, width one, spacing control, only visible word `.`,
immediately before the `CEMENU` opcode token) and validation may exclude it
exactly the way it already excludes a compact display marker.

Still fail-closed: 33 topics whose target's header title is corrupt in a way
no positioned evidence resolves — an `ST ` opcode glued into the title
(SC34-425 1.8.3, 2.10.4), a header taken from a different record entirely
(SC34-425 2.3.19, 2.7.2, APPENDIX1.1.4, IBMMMSTR 4.3), or a TOC title that is
itself wrong (N2AH1MST 1.2 `... JES3 System Access`, SC26-457 3.14.2
`... Example 8 //`).  These need the topic-title extraction fixed at source,
not a comparison relaxation.

## Measurement

* Coverage `bootrace --coverage` over all 34 fixtures: **4,103 → 4,421 /
  7,362**, 318 topics moved legacy → typed, none the other way.
* Whole-corpus `boo2git --force`: 328 files changed, 0 added, 0 removed.
  318 are the moved topics; the other 10 (SC09-2417-00 x7, packet x3) are
  topics already typed on both sides where a leading row-control slot glyph
  (`(`, `[`, `*`, `-`, `"`, `*/`) no longer leaks.  Hosted SC09-241 DT
  `19961114175628` `BIBLIOGRAPHY.1` and `2.1.3.5` confirm the glyphs are not
  displayed.
* Hosted word-level sample: 34 moved topics across 12 books (including
  XWEBDEMO and GG24-4302-00).  Typed better on 18, equal on 16, worse on 0;
  19 word-identical to hosted against 10 for legacy.  The only remaining
  difference class in the sample is the Markdown anchor element
  (`<a id="HDR..."></a>`) both routes emit where hosted names an existing
  element.

Hosted ids/DTs used: XWEBDEMO `19970423182524`, GG24-4302-00
`19950308184737`, ITPPIBOK `19910628074854`, DREICMST `19911219125856`,
SC34-425 `19921112160049`, SC24-546 `19940323131240`, SH12-565
`19941206115523`, GC28-183 `19930625102617`, GG24-395 `19941215160749`,
SC09-138 `19910321130500`, SC41-485 `19951003131222`, FA1PLMM0
`19910927114801`, SC09-2417-00 served as `SC09-241` `19961114175628`.

## Follow-ups

* The record terminator `.` also closes an `ST`/text payload, and the prose
  body prints it: about 2% of typed topics end a paragraph with a doubled
  period (`Purchase, NY 10577..`, GG24-4302-00 8.0 `... environment..`).
  This predates this slice — it is visible on `88cbc81` too — and belongs to
  the prose row model rather than the menu.
* `ProseTableLinkIR` already lowers a `CSELECT` covering a whole table cell
  line to a cross reference; it carries only an anchor target.  Giving it the
  same `CrossReferenceTargetIR` the prose inline now carries would let the
  table family admit `LNK` selectors in cells as well.
