# Sub-token inline ownership and the prose row margin (issue #58) — 2026-08-29

Follow-up to `prose-font-selector-spans-2026-08-29.md`, which left the residual
span cluster at "span starts inside a word" 146, "font/selector span exceeds the
display line" 129, "span ends inside a word" 111 and "font/selector span is
blank" 24 (410 topics on the coverage sweep of that day).  The normative format
facts derived here are in `Format/markup.md`, sections "Sub-word span
boundaries", "The three-column left margin" and "A span holds its row open";
this note keeps the procedure, the measurement and the residuals.

## Procedure

The decisive tool was an automated hosted comparison of the *rejection message*
itself.  Every rejection in this cluster quotes the display row the model built
and the span it could not place, so for each one:

1. fetch the topic from the hosted BookServer (latin-1, `curl`), split its body
   into display lines and record for each line the plain text and the display
   column at which every inline tag opens;
2. find the hosted line with the same alphanumeric content as the quoted row;
3. report `delta` = hosted column of the row's first word − our column.

`delta == 0` means our row geometry agrees with the reader and the span really
does fall inside a word; a non-zero `delta` means the row's left margin is
wrong and the span boundary is an artefact.  Over the 410 rejections this split
the cluster cleanly:

| delta | topics | what it is |
| ---: | ---: | --- |
| 0 | 89 | genuine sub-word highlight |
| 3 | 73 | a change bar in the row's marker slot |
| 1 | 19 | a bullet directly behind a change bar |
| 5 | 14 | a bullet row whose margin the model does not reproduce |
| −2 | 16 | a stray glyph in front of the row's origin run |
| other | 27 | mixed |
| no hosted line | 172 | rows the model merged, split or truncated |

## What each residual class turned out to be

### Genuine sub-word emphasis (89 topics) — admitted

The stem of a word is a dictionary token and its suffix is compiled into the
same token, so the `CFONT` triple addresses part of one decoded word.  Hosted
examples, verified per book: SC09-138 `3.3.1` `cfont 24 5 4` →
`using <TT>CLIST</TT>s.`, SC09-138 `6.2.8.4` `cfont 7 4 2 11 3 3` →
`<B>EDCK</B><B><I>nnn</I></B>`, SC09-138 `8.1.10.7` `cfont 18 7 X 25 3 1` →
`<TT>__dsorg</TT><I>xxx</I>`, GC23-046 `6.0` `cfont 43 1 1` →
`SMPWRK<I>x</I>`, SG24-204 `5.2.1` `cfont 33 1 7 34 1 2` →
`<B><U>L</B></U><B>U</B>`, ACPZMST1 `4.6` `cfont 3 3 V` → `<var>tpn</var>.`,
packet `2.1.1` → `not write<kbd>N4ABC-0</kbd>`.

**Design.** `DocumentSourceSliceIR` gains `character_begin`/`character_end`, a
byte range inside the decoded word of one token (`character_end == 0` keeps the
old whole-token meaning, and a partial slice must name exactly one token).
`ProseTokenDispositionIR` gains `claims`, the ordered list of
`{block, inline, character_begin, character_end}` that partition the token's
decoded word.  `build_block` now walks display *cells*, records each cell's byte
offset inside its token, and merges the cells of one inline into one claim per
token; a claim that covers `[0, length)` compresses into an ordinary
whole-token slice, anything else becomes a single-token slice with the byte
range.  Rows that own their source verbatim (drawn boxes, `cz OFF XMP`) claim
whole tokens through the shared `claim_token_whole` helper.  The verifier
rebuilds the claims from the inline slices and requires them to match the
ledger exactly and to cover each word with no gap and no overlap, so two
inlines can meet inside a word and still leave every display character owned
once.

### The three-column left margin (73 + 19 topics) — fixed

Every reflowed prose row hosted serves starts with a three-column margin, `   `
or ` | ` when the row is revised, and the operand columns count from column 0 of
that margin.  A `U+2502`/`|` width-1 token in the row's marker slot *is* that
margin, so the row's origin run measures only the indent behind it; a bullet
behind the bar keeps the assembler's space, standing at column 3 with the text
at column 7.  Byte-level: ACPZMST1 record 459 tokens 116..118 are `U+2502`, a
four-cell origin run and `XC`, and hosted `8.14.1` (DT 19920319123146) serves
` |     <TT>XC_NOTIFY_MSG</TT>, VM PWSCS ...` for `cfont 7 13 4`.  GG24-395
record 33 tokens 133..138 are a five-cell fill, a one-cell origin, `U+2502`,
`U+2666`, a two-cell gap and `Chapter`, and hosted `PREFACE.3` serves
` | °   <B>Chapter</B> ...` for `cselect 7 37 HDRHWIC100`.

### "exceeds the display line" (129 → 35 topics) — fixed

Nearly all of these were rows the model ended too early, not spans that were
too long.  Hosted keeps going where we stopped: SC24-546 `4.3.1` serves
`     <samp>ABBREV('Print','Pri')</samp>      -><B>    1</B>` as one row
(`cfont 5 21 E 32 2 2 38 1 E`), IEAC6MST `5.1.2` serves
`          <samp>RECORDSIZE(384 3072)</samp> <samp>)</samp>`, SC09-138 `8.4.2.5`
serves `          a = b*(x*y*z);            /* Duplicates recognized */` whose
`;` the row model had read as the next display line's length byte.

The rule adopted: a span of the open row that starts at or beyond the cells
written so far proves the row has not ended, so the one-byte token standing
there is display text.  It is gated twice, because the unguarded form regressed
44 topics into `overlapping font spans`:

- the record's own length-prefixed display lines must parse *and* must not name
  the token as a line's length byte (`Format/logical-controls.md`, "Display
  Lines Inside A Record Payload").  Without that gate a control introducing the
  *next* row held the current one open — GG24-4302-00 `PREFACE.2` then printed
  the `:H3` of the following heading as prose;
- decoder placeholder runs stay slots whatever the geometry says, since they
  carry no character.

### Rows whose margin is still unproven (61 topics) — fail closed

A span that starts or ends inside a word *and* covers a blank display column
cannot be a sub-word highlight: hosted gives each highlighted phrase its own
triple, and a triple never reaches from inside one word across a gap into the
next.  Such a row is displaced, so the topic fails closed with
`row columns are unproven`.  Checked against hosted for the 61 residual topics:
only 2 have `delta == 0`, the other 59 are genuinely displaced rows (17 bullet
rows at `delta 5`, 14 stray-glyph rows at `delta −2`, 10 change-bar rows at
`delta 3` reaching the row through a different branch, and 18 whose row the
model merged or split so no hosted line matches).

An earlier and weaker form of this guard — "a single common column shift makes
every span of the row word-aligned" — was tried and rejected: hosted showed 30
false positives (SC09-138 `3.3.1`'s lone `CLIST` span is re-aligned by a shift
of −6 onto `using`), so a coincidental shift is not evidence.

### "blank span" (24 → 28 topics) — still open

Unchanged in kind: the span covers only trailing padding of a row fragment the
model split off (SC26-457 `A.3.1` builds the row `are       ` where hosted has
`   catalog's entries are:`).  These are row-fragmentation defects that show up
in the span check; the span itself is a legitimate no-op.

## Handed back: QSYSINFO `APPENDIX1.4.2.*`

The table slice handed over a Layout IR defect: QSYSINFO record 631 run 8 ends
at token 226 with `… Automatic Installation`, and `Guide` (token 226, a width-1
dictionary token) lands in no physical row.  The evidence confirms it is display
text — segment 15 stores `cfont 39 9 1 49 12 1 62 5 1` and column 62 is `Guide`,
and hosted `APPENDIX1.4.2.1` (DT 19910524120827) serves
`   ___     --         SX41-9072        <I>Automatic</I> <I>Installation</I>
<I>Guide</I>`.  The cause is in `extract_layout_ir`: `Guide` + the space run
behind it satisfy the marker/origin boundary test, so a boundary is recorded at
226, the row it opens has no visible text and is dropped, and its tokens end up
owned by nobody.

The obvious repair — drop an inner boundary whose row would carry no visible
word, so the previous row extends over it — was implemented and measured: it
gains **+14 topics** (QSYSINFO 12, SC26-457 1, SC34-425 1) and no book loses
one, but it breaks four other families that read empty rows as structure:
`comment_delivery_ir_synthetic` ("comment-questionnaire run geometry is not
canonical at run 3: kind=10 rows=4"), `comment_delivery_document_lowering`,
`topic_document_lowering`, `sc31_711_row_ownership` (the questionnaire's
"Overall, how satisfied are you with" row joins its answer row) and
`lazy_open_sc31_711_topics`; restricting the rule to markers whose text is an
alphanumeric word of two or more characters still broke all but the table one.
The change was therefore **reverted**, not shipped.  A correct fix needs the
boundary drop to be conditioned on a `CFONT`/`CSELECT` span covering the
marker's own display columns, which means `extract_layout_ir` has to decode the
segment's operand — a Layout IR change the questionnaire and COMMENTS row
models must be re-verified against.

## Measured

Baseline: main `f8607f6`, built from `git archive f8607f6 libgeist` into a
scratch tree.  `bootrace --coverage` over every fixture, N2AH1MST included.

- **5,363 → 5,720 of 7,362 typed topics (72.8% → 77.7%, +357)**; 357 topics
  moved legacy → typed and **none moved the other way**.  28 of the 34 books
  gain; largest: SC24-546 +84, SC09-138 +58, SC26-457 +39, OFCUSEOV +25,
  SC24-5527-02 +25, QSYSNEWG +20, GC23-046 +13, GG24-395 +13, IEAC6MST +11.
- Rejection clusters, before → after: span starts inside a word 146 → 0, span
  ends inside a word 111 → 0, font/selector span exceeds the display line
  129 → 35, font/selector span is blank 24 → 28, overlapping font spans
  11 → 15, and the new `row columns are unproven` 0 → 61.  The span family as a
  whole falls 410 → 139.
- Whole-corpus `boo2git --force` before/after (N2AH1MST excluded from the
  export, as in earlier runs): **370 changed topic files, 0 added, 0 removed**
  — 356 moved topics (every moved topic of an exported book) plus 14 topics
  that were already typed and whose rendering these fixes correct.

## Hosted verification

Both samples compare hosted body words and hosted inline markup
(`<B>`/`<I>`/`<U>`/`<samp>`/`<kbd>`/`<var>`/`<cite>`/`<dfn>`/`<tt>`/`<a href>`)
against the Markdown of both builds, with Markdown syntax and escaping
stripped, `<a name>` excluded, verbatim fenced rows excluded from the markup
comparison (hosted serves the same rows sometimes plain and sometimes per-word
`<samp>`), and hosted-only row decoration (the change bar, the `°` bullet)
excluded from the word comparison.

- **90 moved topics across 23 books**: typed better on 86, equal on 3, worse on
  1; **49 are word- and markup-identical to hosted**.  Books absent from the
  hosted catalog (SC24-5520-00, SC24-5527-02, SC28-1881-05, packet) and
  N2AH1MST are excluded from the sample.
- **13 of the 14 already-typed topics whose export changed** (SC24-5527-02 8.1
  is not hosted): better on 2, equal on 11, worse on none.  The two
  improvements are ACPZMST1 `4.6`, where the emphasis moves off the sentence
  period onto the word (`tpn*.*` → `*tpn*.`, hosted `<var>tpn</var>.`), and
  SC09-138 `4.7.2`, where a row that had been split rejoins
  (`**Note**` + a new paragraph → `**Note:** The values 2100-2107 ...`, hosted
  `<B>Note:</B>    The values ...`).

| Difference class | Typed behaviour | Decision |
| --- | --- | --- |
| Sub-word boundary (`<TT>CLIST</TT>s`) | two inlines share the token | admit: the ledger owns the byte range |
| Span splits a word *and* crosses a gap | rejects the topic | fail closed: the row's margin is unproven |
| Hosted-only change bar `\|` and `°` bullet | dropped / Markdown list marker | keep: the prose family's documented convention |
| Figure caption repeated as image alt text (GG24-395 3.2.2.5) | `![caption](resource)` plus the caption line | keep: alt text is not visible text; pre-existing in the figure family |
| Verbatim block rows hosted marks `<samp>` per word | one fenced block | keep: the fence is the monospace marker |

## Residual span-family rejections

`row columns are unproven` 61, `font/selector span exceeds the display line` 35,
`font/selector span is blank` 28, `overlapping font spans` 15.  The first three
are all row-geometry gaps of the same kind (a margin the model cannot derive
from the stored tokens); the hosted delta of each is recorded above.
