# Preformatted (ASCII-drawn) figure block admission sweep, 2026-08-28

Scope: the preformatted body kind of the typed figure block
(`libgeist/src/figure_block_ir.cpp`, `figure_document_lowering.cpp`) for
`SRFIG ... SREFIG` regions without a picture selector. Not wired into the
dispatcher; corpus Markdown unchanged (29 `boorender --md` spot checks
byte-identical against a build of main `afc62c0`). Tracker: issue #58.

## Geometry model

A logical record payload is a sequence of `<length byte><tokens>` display
lines (byte evidence in `Format/logical-controls.md`, "Display Lines Inside
A Record Payload"). The tokens of a line with the decoder's inter-token
spaces are the hosted `<pre>` columns; the length byte is what the Layout IR
reads as a width-1 marker slot. The body of a drawn figure is the lines
between the `SRFIG` line and the caption/`SREFIG` line, with surrounding
blank lines and rule-only frame lines as spacing.

## Hosted comparison

Body lines compared with the hosted BookServer `<pre>` output (same DT
values as `typed-route-census-2026-08-28.md`), scratch tool `figdump` +
`cmp2.py`:

| Book | Topic | Anchor | Body lines identical | Caption |
|---|---|---|---|---|
| FA1PLMM0 | PREFACE.3 | FIGFIGUNIQ1 | 10/10 | none (hosted none) |
| ACPZMST1 | 1.2.5 | FIGRSCOM2 | 12/13 (arrows) | identical |
| ACPZMST1 | 1.2.5 | FIGCOMP | 12/17 (arrows) | identical |
| GG24-4302-00 | 3.3.4 | FIGRMFWL01 | 43/43 | identical (two-line caption joined) |
| GG24-4302-00 | 3.3.4 | FIGRMFWL02 | 53/53 | identical |
| PRG1SORT | 1.1.2 | FIGSTEPS | 24/26 (arrows) | identical |
| SG24-204 | 3.1 | FIGVSEHW1 | 19/19 | identical |
| QSYSNEWG | 2.1 | FIGFIGUNIQ13 | 28/28 | none |
| GC23-046 | 6.2 | FIGDDCAT | 41/41 | identical |
| SH20-918 | FRONT_1.3 | FIGFIGUNIQ6 | 37/37 | none |
| SC34-425 | 1.3.4 | FIGDEVPROC | 37/47 (arrows) | identical |
| SC24-546 | 3.4 | FIGFIGUNIQ19 | 23/23 | none |
| SC09-138 | 1.3.1 | FIGBIO1 | 38/48 (arrows) | identical |
| SC09-138 | 1.3.1 | FIGBIO1SUB | 35/35 | identical |
| DREICMST | 1.1.1.1 | FIGHDFT | 33/36 (arrows) | identical |
| SC24-5520-00 | 1.1.26 | FIGSEGEXT | 13/15 (arrows) | identical |

All 33 differing lines differ only in arrow words (`U+2190`-`U+2193`): the
typed block keeps the arrow glyph, hosted shows the book's display-table
byte (`ÿ`, `"`, or nothing, which shifts the rest of that line by one
column). Everything else, including the right box border overwriting
132-column RMF report text, bullets (`°`), dashed rules and blank interior
lines, is identical.

## Legacy `boorender --md` differences

Compared with the same topics rendered by the legacy string pipeline:
the anchor id is truncated (`BIO1`, `RMFWL01` for `FIGBIO1`, `FIGRMFWL01`);
boxes are padded to 82 columns and the right border misplaced; wrapped CFONT
rows are glued into one line (FA1PLMM0 PREFACE.3) and listing lines are
merged or lost (SC09-138 1.3.1); report rows are emitted twice, once as
prose and once inside the fence (GG24-4302-00 3.3.4); bullets and arrows are
dropped (SH20-918 FRONT_1.3, ACPZMST1 1.2.5); whole figures and their
captions are missing (ACPZMST1 1.2.5 FIGRSCOM2); rule frames are drawn as a
box.

## Sweep

Every `SRFIG` region of every TOC topic (`scratch sweep.cpp` over the figure
extractor, `verify_figure_blocks_ir` and the document lowering verifier on
every admitted block; 0 verification failures):

| | before (`afc62c0`) | after |
|---|---|---|
| regions | 1,766 | 1,766 |
| admitted | 306 (pictures) | 1,247 (305 resource, 1 external, 941 preformatted, 22,928 body lines) |
| declined: no picture selector | 1,371 | 0 |
| declined: figure region contains a table (SRFIG wrapping SRTBL; table family) | 0 | 300 |
| declined: display line prefixes misaligned (length byte >= token threshold, tokenized as a two-byte token) | 0 | 60 |
| declined: prose row inside a picture figure | 54 | 54 |
| declined: picture selector is table-owned | 32 | 32 |
| declined: drawn figure contains a selector | 0 | 21 |
| declined: figure region contains a menu | 0 | 21 |
| declined: unterminated before the next SRFIG | 16 | 16 |
| declined: several pictures | 3 | 3 |
| declined: interior structural control (SRSPT*, SRCCLS, SRTF5, SRV*) | 0 | 8 |
| declined: control line carries payload / text after caption / unterminated | 0 | 2 / 1 / 1 |

Topics with any figure region: 1,185. Fully admitted: figures class 133 of
138 (was 0), mixed class 692 of 1,036 (was 162); partially 33.

Both columns were measured with the same scratch sweep tool over the same
1,766 regions, the `before` one linked against a build of `afc62c0`. No
region admitted as a picture figure before is admitted differently after
(region-by-region diff over all 1,766); the 941 preformatted regions are
purely additive.

## Independent re-verification

Re-run on `afc62c0` + this workload with a freshly built baseline:

- hosted line-for-line over 23 drawn figures in 13 books (fresh fetches):
  **582 body lines, 544 identical, 38 differing**, every difference an arrow
  word (`←`/`→`/`↑`/`↓` against hosted `\x1b`, `ÿ`, `"`, `\x19`) at the same
  column count; every caption identical;
- 31 `boorender --md` spot renders byte-identical against the `afc62c0`
  baseline build (the block is not wired into the dispatcher).

Follow-ups: read a length byte at or above the token threshold as a length,
not a two-byte token, in the record decoder (60 regions, PRG1SORT 1.1.3.1);
apply the book's display translation tables so arrow words match hosted;
stitch a caption's wrapped continuation line across intervening `SI`
index-entry lines (1 region: PRG1SORT 1.1.4.3.2 `FIGSELCDF`, currently
declined as "text after its caption", which is the fail-closed behaviour and
the fixture for that decline).
