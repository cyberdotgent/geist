# Typed renderer output defects, 2026-08-30

Scope: the accumulated output defects that earlier coverage slices recorded
rather than fixed (issue #58). Every decision below is taken against the
hosted BookServer and verified over the whole 34-book corpus. Coverage moved
6,986 -> 6,987 of 7,362 typed topics; the whole-corpus `boo2git --force`
differential is 594 files, each one a fix listed here.

## Hosted comparison procedure

Hosted pages were fetched directly (`urllib`, latin-1), one file per topic in
a local cache:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/<BOOK>/<TOPIC>?DT=<dt>
```

The `DT` value is optional for a book the catalog holds only once and
mandatory otherwise (`SC28-1881-05` and `SC24-5527-02` return a version-picker
page without it). The `DT` of every fixture is its own build timestamp, which
`booinfo` prints as `Timestamp: MM/DD/YY HH:MM:SS`; the 34 values were
generated from `booinfo` rather than transcribed.

Topic ids for exported files come from the book's generated `README.md`
(`- `<id>` [title](file.md)`), which is exact where slugging rules are not.

## 1. Arrow words and the `°` bullet: adjudicated, not applied

Recorded by the ASCII-figure and figure/table slices as "the book's display
translation table, which libgeist does not apply". Both halves of that are
now settled, with the table and the byte evidence in
`Format/encoding.md`, "Hosted Display Bytes For The Non-ASCII Graphic Words":

| Token word | Character | Hosted display byte |
| --- | --- | --- |
| `0x2190` | `←` | `0x1b` |
| `0x2191` | `↑` | `0x22` (`&quot;`) |
| `0x2192` | `→` | `0xff` |
| `0x2193` | `↓` | `0x19` |
| `0x2666` | `♦` | `0xb0` (`°`) |

- It is **not** per book: the same five bytes appear in ACPZMST1 `1.2.5`,
  SC09-138 `1.3.1`, SC34-425 `1.3.4`, PRG1SORT `1.1.2` and DREICMST
  `1.1.1.1`.
- The page the reader's `BooLoadTranslationTablePage` formula names
  (`directory_page_number + (word >> 11)`) holds dictionary data in
  ACPZMST1, SC09-138, PRG1SORT and SC34-425: the entry for the token word of
  `A` is not `0x41`, and the arrow entries are unrelated to the hosted bytes.
  No 256-byte table anywhere in ACPZMST1 maps EBCDIC `0x2b`/`0x2a`/`0x15`/
  `0x04` to `0x1b`/`0xff`/`0x19`/`0xb0` either.

`libgeist` keeps the Unicode characters deliberately: its Markdown is UTF-8,
where `0x1b` is a control byte and `0xff` is not a valid encoding of `ÿ`.
The choice is free: **all 564 preformatted lines in 141 topics of 12 books**
that contain one of these characters are byte-identical to the hosted line
once the table is applied -- no column shifted, no character dropped
(ACPZMST1, DREICMST, FA1PLMM0, GC23-046, ITPPIBOK, PRG1SORT, SC09-138,
SC24-546, SC24-5520-00, SC28-1881-05, SC33-033, SC34-425). A hosted diff
should apply the table before comparing.

## 2. Message anchors

Recorded as R5 in `sc31-711-m10-audit-2026-08-28.md` ("anchors use the first
operand word (`MSG 1`); hosted uses the full operand"). Hosted actually
serves `MSG 1` for SC31-711 `4.1.1` and `MSG bridgeHistoryDataComplete` for
`4.1.2` -- the first word *is* the whole operand there. The defect shows only
where the operand has a symbolic tail: `4.3.2` is served as
`<a name="MSG 256 (snmp_br_dot1dStpPortState)">` and `4.4` as
`<a name="MSG 1 (fddiRPUNoResponse)">`. `TrapEntryIR::operand` already
carries the tail (the SRMSG payload completes it), so the anchor now uses it:
41 anchors over 2 topics, each verified equal to the hosted `<a name>`.

**Not fixed, legacy route:** SC24-546 `A.0` (49 anchors) and SC34-425
`APPENDIX1.5.3` (78) go the other way -- `markup.cpp` builds `MSG ` + the
whole *operand range*, which takes in the next display line's length byte, so
`MSG FLM04005 /` and `MSG DMSREX459E ->` where hosted serves `MSG FLM04005`
and `MSG DMSREX459E`. Those topics are legacy-routed (`bootrace --coverage`:
`prose topic rejected: body control SRMSG is outside the prose model`), the
fix belongs in the same string pipeline two other agents were rewriting this
cycle, and it disappears when the topics reach a typed family.

## 3. Figure alt text and the unresolvable resource destination

Hosted names the image after the picture, never after the caption:
`<img src=".../P69.GIF" alt="PICTURE 69">` (GG24-395 `3.3.8`,
DT 19941215160749) and `<img src="/bookmgr/monetcoq.jpg"
alt="/bookmgr/monetcoq.jpg">` (XWEBDEMO `1.4.1`, DT 19970423182524), with the
caption on the line under the image -- which is where the Document IR
renderer already puts it.

The same figures also carried a broken destination: the renderer wraps a
destination in angle brackets, and `boo2git`'s `resource:` rewriter only
matched the bare `(resource:` spelling, so every typed figure kept
`](<resource:1>)` instead of the extracted PNG. 241 images over 170 topics,
each verified against the hosted `alt` attribute.

## 4. Nested blocks inside list items: still open

Unchanged. `ListItemIR` holds inline content only, so a trap entry is one
list item with its fields run together, and SC31-711 `4.1.2`'s `°` action
list (hosted: three `°` lines after a `<p>`) reflows into the field text, as
does QS3X36CM `1.1`'s inner ASCII-dash list. The recorded flag
`TrapTextIR::paragraph_break` marks *that* a break exists but not where; a
fix needs the break offset as well as `ListItemIR` block children, a
recursive verifier/formatter/renderer, and a re-pin of the trap-catalog
tests. It is a slice of its own and was not attempted here.

## 5. The stray comma in an `SRTBL` object id

`boorender` emitted `<a id="TBLTBLUNIQ155,">`. SC24-5527-02 record 145 spells
the control in token 128 and follows it with token 129, the record encoder's
control separator (encoded value 2, words `{0x0001, ','}`, the same token
already documented in `control_ir.cpp`). The separator carries no space, so
the flattened projection glues its comma to the opcode word. Hosted serves
`<a name="TBLTBLUNIQ37">` (DT 19921218151459).

Only the opcode *spelling* drops the separator; its byte stays inside the
opcode range, so the ownership ledger is untouched. 33 anchors over 27
topics, all hosted-confirmed. SC33-033 `4.8` also reaches the typed route as
a consequence -- its record 186 segment 3 is `SRFIGFIGUNIQ8,`, so the figure
region carried the comma in its object id and the topic failed closed -- and
now serves hosted's
four anchors `LEN CHBAR`, `SPTCHBAR`, `TBLTBLUNIQ7`, `FIGFIGUNIQ8` with the
words the legacy render dropped (`as indicated by`, `second component`, the
`APL code 795` table).

## 6. `#MONET1` versus `#FIGMONET1`, and 1,140 other broken links

The reported XWEBDEMO defect is one instance of a corpus-wide class:
**1,352 of the corpus's 1,591 anchor links pointed at anchors that do not
exist.** `boo2git` builds its link map from the legacy GML projection, whose
`:fig id="MONET1"` drops the `SRFIG` prefix the source control spells, while
both renderers write the anchor hosted writes (`<a name="FIGMONET1">`). The
same applies to `:table id="TBLUNIQ14"` against `<a name="TBLTBLUNIQ14">`.

A destination whose anchor its own file does not contain is now repaired
against the anchors that file really emits, and only when exactly one of them
ends with the destination's id; a destination that already resolves is never
touched. **1,140 links repaired over 397 files, every one verified equal to
the fragment hosted serves for the same reference (`href="...#<fragment>"`),
0 worse, 0 newly broken.** The 212 that remain point into topics that emit no
anchor at all -- a legacy-route gap, not a link-map one.

## 7. The comma SH12-565 3.1.6 dropped

Recorded as costing 60 topics to recover. The measured 60-topic cost belongs
to the *wide* rule (give the demoted segment the whole gap back to the
previous segment's end, padding included). The narrow one is free.

The splitter fires on the `,` in front of `SRV=(3,2,2)` -- a word that only
matches its `sr` prefix -- and leaves the comma in neither segment. In front
of a real control that is right: hosted prints no comma there (case 5 above).
Here the segment carries no control, and the record's own display line has
the comma: `bootrace --lines` shows record 205 line 7 as
`       F QH,F XY,SRV=(3,2,2)`, and hosted serves
`<kbd>F</kbd> <kbd>QH,F</kbd> <kbd>XY,SRV=(3,2,2)</kbd>` (DT 19941206115523).

`reclaim_split_separators` gives back exactly one separator token, only into a
segment that carries no control, and only out of the one-byte gap the split
left. Whole-corpus differential: **one file**.

An earlier attempt inside the splitter itself (keep the comma at the head of
the following segment, where the existing leading-separator handling reads
`, sh2.2.1`) was measured and reverted: it costs **3,367 topics**
(6,987 -> 3,620 typed). Recorded so nobody tries it again.

## Verification

- Whole-corpus `boo2git --force` before/after over all 34 fixtures: **594
  files differ, every one a fix**: 397 anchor-link repairs, 170 figure alt
  texts (+1 external), 27 table anchors, 2 message-anchor topics, SC33-033
  `4.8` (legacy -> typed), SH12-565 `3.1.6` (the comma). No other file of the
  7,362 changed.
- Coverage: `bootrace --coverage` per book, `typed=6987 legacy=375`.
- `ctest -LE slow -j6` 49/49, `ctest -L slow -j3` 15/15.
- Six pinned expectations moved, each hosted-checked first: two figure alt
  texts in `lazy_open_gg24` (whose own comment already recorded hosted's
  `alt="PICTURE 9"`), XWEBDEMO's external image, SC31-711 `4.4`'s first FDDI
  anchor, and the two synthetic renderer pins; `lazy_open_sh12_565` gained an
  assertion for the recovered comma.

## Process note

The session scratchpad is shared between the agents running in parallel: one
of them replaced `scratchpad/export.sh` with its own version pointing at its
own worktree while this slice was using it, which silently exported the wrong
build. Keep per-agent scripts in a per-agent subdirectory and check the
binary path in any script you did not write in the same session.
