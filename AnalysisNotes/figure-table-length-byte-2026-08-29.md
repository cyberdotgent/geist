# Figure and table residuals: the display-line length byte, 2026-08-29

Slice of issue #58 aimed at the remaining figure and table decline classes
(~136 topics).  Three of them turned out to be one fact read three different
ways, and one was a structural fact about how figures nest.

Typed-route coverage over the 34 BOO fixtures: **6,544 -> 6,654 of 7,362
(88.9% -> 90.4%)**, measured with `build/bootrace <book> --coverage` before
(a build of `8343096`) and after.  110 topics moved to the typed route, none
moved off it, and every book's count grew or stayed equal.

## 1. A length byte at or above the token threshold (26 topics)

`figure '<id>' declined: display line prefixes of record N are misaligned`,
all 26 in PRG1SORT.

PRG1SORT's token threshold is `0xd6`.  Record 47 byte `0xe5ae` holds `0xdc`,
the length of a 220-byte display line.  The left-to-right token reader takes
`0xdc` for the first byte of a two-byte dictionary reference, reads
`0xdc18` = `classification`, and from there every following length byte lands
in the middle of a token, so `record_display_lines` declines the whole
record.  Hosted DT `19900829171904` prints no `classification` in
`1.1.3.1`.

`decode_record_payload_ir` now re-decodes the payload line by line whenever
the plain walk disagrees with the record's own line structure, and adopts the
re-decode only when it consumes every line exactly.  Where the plain walk
already parses as display lines the two agree by construction, so nothing
else in the corpus changed.

Recorded in `Format/logical-controls.md`, "A Length Byte May Be At Or Above
The Token Threshold".

## 2. A length byte whose spelling is a control name (56 topics)

`figure region contains a menu` (17), `body control cbacklevel /
cforwardlevel / chdlevel / cmitem / cparent / csourcefn is outside the prose
model` (43 across the spellings), `segment is not a well-formed font control`
(12) -- one defect.

FA1PLMM0 record 477 byte `0x3f00f` is `0x39` = 57 and opens the 57-byte
display line

```
                    LTAB=(10,00,05,10,15,20,25,30,35,40,45,50,56),          *
```

inside the drawn figure `FIGFIGUNIQ9`.  The dictionary spells `0x39` as
`cmitem`, so the control-segment decoder read a menu item there and made the
rest of the line its payload.

Genuine controls always sit at `prefix_token + 1`: FA1PLMM0 record 471
spells `ctopicn`, `cparent`, `cforwardlevel`, `cbacklevel`, `csummary`,
`chdlevel`, `csourcefn`, `SRHDRPWRGEN`, `ST`, `SI` and `cfont` each one token
after its own line's length byte.  `demote_display_line_owned_controls` now
demotes any segment whose opcode is spelled by a length byte to display text.

Two pinned negatives became positives and were re-pinned:
`SC28-1881-05 1.6` admits its `ATTRIB` syntax diagram, and `SC31-711 4.3.5`
composes its trap catalog -- word-for-word equal to hosted DT
`19941010174546` (`1` / `Description:  DLCI state change` / `LNM for AIX
Response:  Poll the port.`), where the `cbacklevel` the introduction appeared
to carry was a length byte.

## 3. Figure regions nest (14 topics)

`figure region has no picture selector (unterminated before the next SRFIG)`.

All 14 have the same shape: an outer `SRFIG<caption>` wrapping an inner
`SRFIG<tbl>` + `SRTBL<tbl>` ... `SRETBL` `SREFIG`, closed by a second
`SREFIG`.  DREICMST `1.2.1` records 79-84 are the reference case and hosted
DT `19911219125856` serves both anchors (`<a name="FIGLOGPROC">   split=yes.
</a>`, then `<a name="FIGXXX"><a name="TBLXXX">` on the table's top rule).

Three changes were needed together:

1. the extractor keeps a stack of open regions instead of one;
2. a table anywhere inside a region makes it the table family's *frame*
   before any other structural test, because the inner `SRFIG` opener stands
   one segment ahead of its `SRTBL`;
3. a frame may carry display lines of its own beside the table it frames.
   `plan_frame` used to require every unclaimed token inside a frame to be
   invisible; now a whole display line that carries no table-owned token
   stays unassigned for the stream pass to lower as body text (the
   `split=yes.` lead line and the `Figure 5. ...` caption), a length byte is
   structure, and a visible token on a table-owned line is still the
   conservation failure it was.

Six of the 14 reached the typed route; the other eight fell through to
classes owned by other slices (`cz OFF LBLBOX is not closed`, `SRFTNSPE`
footnote controls, `token has no disposition`).

Recorded in `Format/markup.md`, "Figure regions nest".

## 4. A length byte inside a table or figure region (12 topics)

`visible token 'X' inside the table region of record N token N is claimed by
no block`, where `X` was `cparent`, `citerm`, `chdlevel`, `cfont`,
`checkpoint`, `charts`, `in`, `job`.  Every one is a display line's length
byte: GG24-395 `COMMENTS` record 826 token 0 is byte 65 and spells `cparent`;
SH20-918 `3.16` record 216 token 52 spells `cfont`.  No block claims such a
byte because it is not display material, so the region conservation check now
admits it, exactly as it already admitted a table marker slot.

## Tried and not shipped

* **QSYSINFO's table cell-column mapping** (12 topics, handed over by the
  previous slice).  The Layout IR half was diagnosed exactly: record 643
  token 204 is `Guide`, the last word of the display line
  `                    Programmer's Guide`, and token 205 is the next line's
  length byte `17`; the pair reads as a marker/origin boundary whose row
  carries no display text, the row is dropped and `Guide` with it, while the
  line's own `cfont 39 12 1 52 5 1` and hosted DT `19910524120827`
  (`<I>Programmer's</I> <I>Guide</I>`) both put it in the title cell.  Three
  repairs were measured and all three reverted:
  - refusing a boundary whose *origin* is a length byte gained 16 topics and
    lost 1, and broke `glossary_production` ("glossary lost canonical
    content") and `sc31_711_row_ownership` -- a row genuinely does wrap onto
    the next line and take that line's length byte as its origin;
  - withdrawing the empty row's boundary whenever the marker is not a length
    byte broke the same two tests plus `topic_document_lowering` and
    `comment_delivery_*`;
  - measuring the open row's text from *after* its own length byte (which is
    what makes the margin shift negative and keeps the boundary) reproduced
    the regression the previous slice had already measured and reverted: it
    costs the `?` of `the information in this book?` in SC31-711 COMMENTS.

  The same disproof applied one layer up, in the gap-table line splitter
  (`match_origin`), *does* recover the word -- QSYSINFO
  `APPENDIX1.4.2.1.7` then reproduces hosted's `<pre>` line for line -- but
  the envelope stops composing as a table and six other topics fall off the
  typed route, so it was reverted too.  The residual is one word per topic in
  12 QSYSINFO topics, which are typed either way.

* **A length byte that reaches prose as body text** (17 topics,
  `visible token N of record N belongs to no block`).  OFCUSEOV `1.20` record
  277 token 97 is byte 31 spelled `(` and opens the `cfont 10 8 2 ...` line;
  QSYSNEWG `7.7.2.1` record 232 token 0 is byte 51 spelled `any`.
  `marker_at` reads a length byte as a row slot only when a space run follows
  it, and a line that starts with a control has none.  Adding the rule at the
  classifier's text fallback changed nothing, so these tokens reach `text`
  through an earlier path that was not located in this slice.

## Left for other slices

`figure region contains prose row` (18), `drawn figure contains a selector`
(11 -- the selector is a cross reference inside the drawn body or its
caption, e.g. SH20-918 `2.1` `FIGSTRUC`, whose caption hosted serves as
`document elements are shown in <a href="...#FIGBDE">Figure 5</a>`; admitting
the figure without a link model in `FigureCaptionIR` would drop that link),
`table contains a picture selector` (11, still the "18 genuine picture
selectors in table cells need a picture cell in the table model" lead).
