# BOO Format Notes

This directory is the canonical home for IBM BookManager BOO file-format facts.
Each topic note should separate verified observations from hypotheses and cite
fixture filenames, byte offsets, hex values, and decoded interpretations.

## Index

| Topic | File | Status |
| --- | --- | --- |
| Container header, directory page, and logical book header controls | [boo-header.md](boo-header.md) | Reader-code and fixture verified for page-0 locator, page-1/version-2 directory fields, and decoded metadata control keys. |
| Logical header-control storage | [logical-controls.md](logical-controls.md) | Reader-code, fixture, and hosted-BookServer verified for tokenized logical-record storage, record framing, token references, dictionary delta transforms, translation-table decoding, and decoded control keys; logical-record spacing controls still need complete field-level documentation. |
| Page organization | [pages.md](pages.md) | Page geometry, the logical-page base, and the three structural page classes verified across all 34 fixtures; the bit meaning of the class word is open. |
| Topic and documentation page storage | [topics.md](topics.md) | Reader-code and fixture verified for topic-start indexes, topic bounds, `SH` topic ids, `CTOPICN`, and TOC id-to-topic resolution. |
| Table of contents | [table-of-contents.md](table-of-contents.md) | Reader-code, fixture, and hosted-BookServer verified for topic-start indexes, topic header controls, `CTOCDEF`, and literal `CTOCE` entries in the `CONTENTS` topic. |
| Decoded markup and controls | [markup.md](markup.md) | Reader-code, fixture, and historical-source verified for GML-derived control names, topic headings, TOC controls, font/highlight spans, links, figures, picture references, and layout controls; exact renderer styling remains partly open. |
| Compression | [compression.md](compression.md) | Reader-code verified: no standard compression layer identified in the BOO page-read path; binary-looking content is currently tokenized/packed BookManager data. |
| Encoding and tokenization | [encoding.md](encoding.md) | Reader-code and fixture verified for dictionary literal byte-to-token-word mapping, token-word translation tables, and the main output-decoding path; full code-page matrix remains open. |
| Security, encryption, hashes, and signatures | [security.md](security.md) | Reader-code verified: no encryption, signature, cryptographic hash, CRC, or checksum algorithm identified in the sampled BOO open/read path. |
| Assets and media resources | [assets.md](assets.md) | Reader-code, Transmogrifier-code, and fixture verified for pre-directory embedded image resources, version 1.2/1.3 legacy picture descriptors, version 1.4 converted object descriptor groups, absolute payload offsets, and the BookServer/ImageMark GIF conversion boundary. |
| Legacy GDF image payloads | [GDF.md](GDF.md) | Reader-code and fixture verified for kind `G` dispatch and current fixture-driven rendering limits. |
| Legacy MMR image payloads | [MMR.md](MMR.md) | Reader-code and fixture verified for kind `I` dispatch, observed wrapper fields, first compressed-segment framing, and IBM reference rendering. |
| Version 1.4 web image payloads | [WebImages.md](WebImages.md) | Transmogrifier-code verified for converted object payload image extensions and dimension extraction behavior. |

## The Corpus, And What "Verified" Means Here

Most counted claims in these notes are measured over the same corpus. Unless a
note says otherwise, that corpus is:

| Quantity | Value |
| --- | ---: |
| BOO fixtures in `BOO/` | 34 |
| Topics declared by their directories (sum of `0x003e`) | 10,502 |
| Logical records inside those topics | 35,109 |
| Display lines inside those records | 895,011 |
| Length bytes resolved back to a source byte | 894,877 |

A claim in `Format/` should say which of these denominators it was measured
against. A claim measured on two fixtures should say so, because several claims
retired below were true of two fixtures and false of thirty-four.

Hosted BookServer at `http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/`
is the rendering oracle, but it is not infallible and must not be cited as if it
were. It truncates `SH20-918`'s `INDEX` before the end, it serves some books at
a different edition than the bundled fixture, and `SC24-5520-00`,
`SC24-5527-02`, `SC28-1881-05` and `packet` are absent from its catalog
altogether. Where hosted and the bytes disagree, say which one the claim rests
on.

## 2026-08-30 Documentation Audit

Every factual claim in `Format/*.md` was inventoried and re-checked against the
corpus and against the current `bootrace` tooling. Claims corrected or retired
in that pass are marked in place with the word "retired" or "corrected" and with
both the old and the new statement, so that a reader who remembers the old text
can see why it changed. The single structural cause of most of them: rules that
were derived from the *flattened decoded string* and are really about the
display-line length byte documented in
[logical-controls.md](logical-controls.md#display-lines-inside-a-record-payload).

### Claims That Cite A Fixture This Repository Does Not Have

Six claims across `Format/` cite `SC26-4221-08.boo`. That file is not in `BOO/`,
is referenced by no note in `AnalysisNotes/`, and none of its identifiers
(`v2pubs`, `hdrlanguag`, `Figv2pubs`) occurs in any of the 34 bundled fixtures.
They are now marked unverifiable in place — four rows in
[markup.md](markup.md) and the version-1.4 descriptor byte table in
[assets.md](assets.md). None of them is known to be *wrong*; none of them can be
reproduced from this repository either. Either add the fixture or re-derive the
claims from a bundled book.

### Where The Code Disagrees With This Documentation

These are places where `Format/` is right and `libgeist` is not, found while
re-verifying. They are recorded here for scheduling; this audit changed no code.

| Symptom | Where | Evidence |
| --- | --- | --- |
| `BooDocument::topics()` returns 10,503 topics for the 34 fixtures where the directories declare 10,502. The extra entry is `SH12-565.boo` `19-6639`, with `topic_number == 0` and record range 906-907. | `libgeist/src` topic enumeration | This is exactly the spurious `SH`-scan split that [topics.md](topics.md) warns against: record 906 is a bibliography order number inside `BIBLIOGRAPHY.2`, it carries none of the nine envelope controls, and it is not in the directory `0x003c` topic-start index. `SH12-565.boo` directory `0x003e` is 296; `topics()` yields 297. |
| `bootrace <book> <topic>` resolves topics through `find_toc_entry`, so it cannot trace any topic that is not in the book's TOC. `IBMMMSTR.boo` has 1,677 topics and 60 TOC entries, so 1,617 of its topics are unreachable from the tool. | `libgeist/examples/bootrace.cpp` | `BooDocument::trace_logical_records` itself looks topics up in `topics_` and works for all of them; only the example's `find_toc_entry` guard rejects them. Every corpus-wide measurement in this audit had to bypass `bootrace` for that reason. |
| `bootrace <book> --coverage` enumerates TOC topics only, so its "total" understates books whose TOC is partial. | `libgeist/examples/bootrace.cpp` | `IBMMMSTR.boo --coverage` reports `total=60` against 1,677 declared topics. |

### What An Independent Implementer Still Could Not Build

Ordered by how much it blocks. Each entry says what is missing, not merely that
something is open.

1. **The token-word to display-byte translation tables are documented as a
   mechanism, not as data.** [encoding.md](encoding.md) gives the table lookup
   (`table_no = (word >> 11) + 1`, entry at `table_page + index * 2`, emit the
   low byte) and the page addressing, which is enough to *decode* a book. What
   it does not give is the book's display glyph set: the arrow words
   `U+2190`-`U+2193` and the bullet `U+2666` reach a hosted page as `ÿ`, `"`,
   dropped and `°` through these tables, and no note says which table entry
   produces which byte. An implementer will decode text correctly and render
   box-drawing and symbol characters wrongly.
2. **Dictionary index controls `2`, `4` and `5` are unexercised.**
   [logical-controls.md](logical-controls.md) documents the reader's behaviour
   for all six control values, but the 34 fixtures only exercise `0`, `1` and
   `3`. An implementer cannot test the other three, and a book that uses them
   would fail without warning.
3. **No worked end-to-end example exists.** There is no place in `Format/` where
   one specific book is carried from byte 0 through page 0, the directory, the
   topic-start index, one record's compact length, its token references, one
   dictionary delta, the translation table, the display-line split and the final
   text. Every stage is documented separately and correctly; the joins are left
   to the reader. This is the single largest practical gap.
4. **Row layout is documented as rules rather than as an algorithm.** The
   display-line model is solid and the length byte is settled, but turning a
   sequence of display lines into paragraphs, lists, tables and figures still
   rests on a set of separately-evidenced rules spread across
   [markup.md](markup.md) with no stated precedence between them. Two sections
   can both apply to one line. An implementer can reproduce hosted output for
   the cited topics and has no procedure for the rest.
5. **`CFONT` column arithmetic is under-specified for the general case.** The
   three-column left margin, the change bar, the bullet and the trailing padding
   are each documented with hosted citations, but there is no single statement
   of how to build the display-column map for an arbitrary line from its tokens.
   The spacing-prefix rules in [encoding.md](encoding.md) are necessary and not
   obviously sufficient.
6. **23 of the 35 font style codes have no verified rendering**, including all
   six admonition codes. An implementer knows the semantic name and not what to
   emit.
7. **`CTOCDEF` operand meanings.** All 34 fixtures define the seven styles
   identically, so the corpus contains no differential evidence at all. This one
   is probably unresolvable without a book outside the corpus.
8. **The trailing `0x0001` logical-record run has no documented purpose.** Every
   book has one; no note says what a reader is supposed to do with it, and the
   book-header parser is documented as reading the content stream instead.
9. **`c.rev` is identified but its effect is not.** No stored per-row revision
   marker has been located, so change bars cannot be attributed to a revision
   code.
10. **Legacy image payloads are decodable but not renderable to spec.**
    [MMR.md](MMR.md) is the strongest note in the directory: an implementer can
    build a working T.4/T.6 decoder from it, including the EOL tag bit and the
    run-length reference-line model. [GDF.md](GDF.md) documents the complete
    `IMGDF2.FLT` order dispatch set and the coordinate encodings, which is enough
    to walk a payload, but its "Current Rendering Scope" section describes what
    `libgeist` approximates rather than what GDDM does: text shaping, font
    metrics, clipping, transforms, fill-pattern semantics, arc geometry, segment
    replay and cell-array color interpretation are all named as approximate.
    An implementer can parse every order and cannot know what to draw for
    several of them. That section is also written in terms of `libgeist`
    behaviour, which the `Format/` charter asks these notes to avoid.
