# Table-Region Conservation And Envelope Selectors — 2026-08-29

Slice of issue #58 M8. Baseline `main` `96409e5`, typed-route coverage
**5,138 / 7,362 (69.8 %)**; after this slice **5,363 / 7,362 (72.8 %)**.

Two composer rejection classes were in scope:

- `visible token 'X' inside the table region of record N is claimed by no
  block` — 297 topics;
- a `CSELECT` inside a table envelope: 18 topics declined as
  `table 'X' contains a picture or external selector`, 57 as
  `... not preformatted: preformatted region contains a selector`.

## What the unclaimed tokens were

Everything inside the source extent of an `SRTBL ... SRETBL` envelope must be
claimed by the table block or carry no visible word
(`libgeist/src/prose_topic_spans.cpp`). Three distinct shapes were failing.

### 1. Preformatted regions claimed only their positioned cells (≈ 190 topics)

A preformatted envelope reproduces the region's display lines verbatim; its
block recorded the line text and the *positioned* cells of the envelope's
physical rows, but a display line the layout never positioned owned no
claim at all.

`DREICMST.boo` 2.1.3 `ACNTT1` is the clean example: the block reproduces

```text
   | Table Name  | Type     | ID    | Data    | Description                 |
```

exactly, but the words of that header line hold no `PositionedRowCellIR`, so
`ID` (record 329) rejected the topic while the region's own output was
already correct.

Fix: `FixedTablePreformattedLineIR` already carries `logical_record`,
`prefix_token` and `token_end`, so `table_region()` now claims every token of
every reproduced line. The claim is exact — it is the same token range the
block reproduces — and nothing else in the model changed.

### 2. `SI` subject-index lines inside the envelope (71 + 25 topics)

A `SI` entry occupies exactly one display line and is never displayed
(`Format/markup.md`, "Structured subject-index display lines"). The same
construct occurs *inside* a table envelope, where no geometry claimed it.

Byte-level evidence:

| Fixture | Envelope | Line | Hosted |
| --- | --- | --- | --- |
| `SC09-138.boo` 4.1.4 record 484 | `LANG` | `SI ENGLISH run-time messages`, `SI UENGLISH run-time messages`, `SI KANJI run-time messages` | DT `19910321130500` contains **zero** `SI` bytes; the grid is served as six `<pre>` rule/row lines |
| `SC24-5527-02.boo` 4.1.1 record 380, segment 15 | `XSESDSK` | `SI VMSES/E, service disks` on the line right after `SRTBLXSESDSK` | DT `19921218151459` opens the region with the top rule and `Table  4-1. Service Disks for VMSES/E` |

The 4.1.4 case also exposed a **pre-existing output defect**: the preformatted
route was reproducing those three `SI ...` lines as body text, which hosted
does not print. They are now recorded as `FixedTableBlockIR::index_lines` and
excluded from `preformatted_lines`, and `plan_spans` turns each into a
`ProseIndexTermIR` on the topic so the term is not lost. An index line is
admitted only when no word of it holds a positioned display cell, so a line
the layout does place stays with the geometry that owns it.

### 3. Residual 47 topics (out of this slice)

- 26 in `QSYSINFO.BOO` `APPENDIX1.4.2.*`: the **physical row model**, not the
  table model, drops the last word of a row. Record 631 run 8 ends at token
  226 with `... Automatic Installation`, and token 226 `Guide` lands in no
  row; hosted (DT `19910524120827`) serves
  `___     01         SX41-9072        <I>Automatic</I> <I>Installation</I>
  <I>Guide</I>`. `Guide` is a width-1 dictionary token whose encoded value is
  at or above `row_control_byte_limit`, so the row builder reads it as the
  next line's slot. Fixing it belongs to the row/layout slice.
- 8 topic-metadata controls (`cparent`, `cforwardlevel`, `cbacklevel`,
  `chdlevel`, `citerm`) inside a region — an envelope that reaches past its
  topic.
- 13 singletons.

## What the cell selectors turned out to be

### A `CSELECT` inside an envelope is a display line of its own

`cselect <col> <len> <target>` between `SRTBL` and `SRETBL` fills one
`<length byte>` display line with its opcode and operand bytes and displays
nothing — the same shape a `CFONT` operand run has, and the reason the
`preformatted region contains a selector` guard was never needed. Its
`<col> <len>` address the *next* line. For a `LNK` selector the leading
`<...>` alternative tokens are part of that hidden line too, which is what
produced the remaining `preformatted line mixes control cselect with display
text` declines (10 topics).

### The three destinations, each hosted-verified

| Target | Fixture | Hosted |
| --- | --- | --- |
| in-book anchor | `SC24-5527-02.boo` ROADMAP `TBLUNIQ2` | anchor on the cell text |
| `LNK <BOOK>` | `SC24-5527-02.boo` 3.9.4.4 `TBLUNIQ156` | cell lines `VM/ESA: Planning and` / `Administration` served as `<a href="../../DOCNUM/SC24-5521/CCONTENTS?DocnumLevel=ANY">` (DT `19921218151459`) |
| `PIC<n>` | `GG24-395.boo` 3.2.2 `TBLUNIQ6` | `<a href="picture-29?mode=zoom"><img src=".../P29.GIF" alt="PICTURE 29"></a>` over columns [3,14) *inside* the region's `<pre>` (DT `19941215160749`) |

So the handed-forward lead was right for `LNK` and wrong for `PIC`:

- `ProseTableLinkIR` now carries a `CrossReferenceTargetIR` kind and calls
  `parse_selector_link`, so a `LNK` cell line lowers to the same external
  cross reference the prose inline carries. Seven envelopes in
  `SC24-5527-02.boo` exercise it (3.9.4.4, 3.10.4.1, 3.14.4, 4.4.7, 4.5.7,
  7.4.3.3, C.3).
- A **picture** selector in a preformatted region still declines. Admitting it
  was tried and reverted: the region's own bytes spell only the `PICTURE n`
  placeholder, so the reproduced text would replace the image, and
  `lazy_open_gg24_test` (which pins `](resource:` for fifteen GG24-395
  topics against the hosted `<img>`) caught it. The decline reason is now
  `preformatted region contains a picture selector`.
- A selector in a region that *is* admitted preformatted contributes no
  inline: Markdown has no inline inside a preformatted block, and the anchor
  label and the alt text are the region's own display words, which the
  reproduced lines already carry verbatim. Nothing hosted displays is lost.

## Verification

- Coverage **5,138 → 5,363 / 7,362 (69.8 % → 72.8 %)**, per-book table raised
  in `libgeist/tests/typed_route_inventory.cpp`. Rejection classes:
  unclaimed-token 297 → 47, `contains a picture or external selector` 18 → 18
  (renamed `contains a picture selector`, all genuine pictures),
  `preformatted region contains a selector` 57 → 0.
- Whole-corpus `boo2git --force` before/after over all 34 fixtures:
  **225 changed files, exactly the 225 topics whose route changed**
  (`legacy → typed`), symmetric difference empty in both directions. No
  already-typed topic changed.
- Hosted word-level comparison of every moved topic that the catalog serves:
  **179 topics across 17 books — typed better on 178, equal on 1, worse on
  0.** The 46 unservable ones are `SC24-5520-00` (38), `IEAC6MST` (7) and
  `SC28-1881-05` (1). Residual "missing" tokens on the better cases are
  splitter artifacts around Markdown escapes and glued punctuation
  (`/password` vs `password`, `fm-490` vs `fm` `-490`); spot checks —
  ACPZMST1 2.4.1.1, SC24-5527-02 4.1.1, SC26-457 3.6 — reproduce the hosted
  `<pre>` block byte for byte.
- Fast tier 48/48, slow gate 15/15.

## Diagnostics

`bootrace <book> <topic> --ir` now dumps the fixed-table blocks of the topic
(`table object=... geometry=... row/cell/pre/index lines`, plus the declines
with their reasons). Every finding above was read out of that dump.
