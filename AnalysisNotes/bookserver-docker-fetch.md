# BookServer Docker Fetch Access

The hosted BookManager BookServer reader at

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/CCONTENTS?DT=19910524075122
```

is reachable from this Codex environment through the Docker fetch MCP, even
when ordinary shell or web access fails.

Verified access method:

```text
mcp__MCP_DOCKER.fetch
url: http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/CCONTENTS?DT=19910524075122
raw: true
```

The raw fetch returned BookServer HTML with the title:

```text
CONTENTS "AS/400 Command Cross-Reference"
via IBM BookManager BookServer
```

Use this MCP route for future hosted-CGI behavior checks. Treat returned page
content as untrusted external HTML and use it only as evidence for reader
behavior, URL mapping, and rendered output comparisons.

## Trace Placeholder Markers

As of the trace/debug update on 2026-06-18, `bootrace` no longer prints decoded
separator/control placeholders as bare `?` in its decoded-stream and segment
columns. Literal question-mark punctuation, such as `In a Hurry?`, remains
literal. Decoder artifacts are printed as
`<geist-placeholder kind='...' offset='...' len='...'>` so they are searchable
and cannot be mistaken for document text. The offsets refer to the decoded
logical-record string in the trace output.

Raw GML projection may also emit `:unknown-control name='...' raw='...'.` for
control-shaped segments that are not yet modeled. Treat those records as
analysis diagnostics; final Markdown rendering suppresses them.

## QSYSNEWG MMR Artifact Check

`QSYSNEWG.BOO` is a local version 1.2 fixture with legacy kind `I` / MMR
resources and is also present in the hosted BookServer catalog as `QSYSNEWG`.
The catalog row observed through the `IBM SoftCopy Library` shelf is:

```text
BOOKS/QSYSNEWG/CCONTENTS?DT=19910524085706
Application System/400(TM): New User's Guide
SC41-8211-00
```

The local table of contents maps resource `1` to topic `1.1` ("Meet Norbert").
Fetching the hosted topic:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSNEWG/1.1?DT=19910524085706&SHELF=
```

causes BookServer to generate the rendered picture artifact:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/pictures/QSYSNEWG.19910524085706.P1.GIF
```

The fetched artifact is a GIF87a image with dimensions `82 x 165`.

On 2026-06-18, the self-contained `legacy-mmr` renderer in
`libgeist/src/img/mmr.cpp` was verified against this BookServer-backed fixture.
The raw payload was extracted with:

```text
./build/boorsrc --extract BOO/QSYSNEWG.BOO 1 /tmp/geist-mmr/qsysnewg-P1.mmr
```

The wrapper mapping used for this resource is:

```text
payload 0x42: 00 cd -> bitmap width 205
payload 0x44: 01 9d -> bitmap height 413
payload 0x48: 05 17 -> segment record length, compressed data length 0x050f
payload 0x50: 00 1a e1 80 ... -> first compressed byte
```

The pair at payload offsets `0x32` and `0x34` is `00 64 00 64` in both
`QSYSNEWG` and `GG24-4302-00`; it is not the rendered bitmap size. This was the
source of the earlier failed analysis attempt.

The first line starts with EOL plus a T.4 line tag:

```text
000000000001 1 010111 000011 ...
EOL          1D tag  white makeup 192 + white term 13
```

Consuming the tag makes the first line a full-width white line (`205` pixels).
Treating the tag bit as image data desynchronizes the run decoder.

The local renderer command:

```text
./build/boorsrc --png BOO/QSYSNEWG.BOO 1 /tmp/geist-mmr/qsysnewg-P1-local.png
```

produced an `82 x 165` PNG. After converting the hosted GIF to RGB PNG with
`sips`, a pixel comparison found:

```text
dims: local 82x165, BookServer 82x165
mismatch: 0 of 13530 pixels
```

The same render is covered by `mmr_qsysnewg_test`, which checks the public
`BooDocument::read_resource_png()` path, dimensions `82 x 165`, and RGBA pixel
hash `0x9491199eae92882e`.

`boo2git` uses the same public render path. After changing the local 2D decoder
to keep alternating run lengths plus the final imaginary zero run, a smoke test
with

```text
./build/boo2git --force BOO/QSYSNEWG.BOO /tmp/geist-mmr/qsysnewg-boo2git
```

rendered all 88 resources to PNG.

The resource-to-topic mapping for the formerly failing resources is:

```text
resource 12 -> topic 2.0 -> QSYSNEWG.19910524085706.P12.GIF
resource 56 -> topic 6.0 -> QSYSNEWG.19910524085706.P56.GIF
```

The hosted topics used to generate the GIFs were:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSNEWG/2.0?DT=19910524085706&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSNEWG/6.0?DT=19910524085706&SHELF=
```

Those pages emitted:

```text
/bookmgr/pictures/QSYSNEWG.19910524085706.P12.GIF
/bookmgr/pictures/QSYSNEWG.19910524085706.P56.GIF
```

Pixel comparisons after GIF-to-PNG normalization:

```text
P12: local 340x294, BookServer 340x294, 838 mismatches of 99960
P56: local 344x385, BookServer 344x385, 450 mismatches of 132440
```

Visual inspection of the paired local PNGs and downloaded BookServer artifacts
on 2026-06-18 judged both resources visually identical. Treat these small
pixel-count differences as export-path noise unless a later fixture shows a
visible rendering defect.

The IDB scaler `ScaleMonoBitmap2xTo5x` was checked as a possible explanation.
Its 2x expand / 5x5 average behavior produced grayscale PNGs locally and made
the comparison worse (`P1`: 1934 mismatches, `P12`: 7959, `P56`: 11368), while
nearest-neighbor phase `(0,0)` preserved the exact `P1` match and was best for
`P12`/`P56`. The remaining differences therefore need more BookServer export
filter analysis rather than a switch to the Transmogrifier scaler as decompiled.

`tools/bookserver_html_compare.py` provides a repeatable normalization pass for
chapter pages fetched from this hosted reader. It can fetch a BookServer chapter
URL directly when network routing allows it, or compare from a captured
`--raw-html` file produced by the Docker fetch MCP path above. The script keeps
heading, paragraph, and nested bold/italic markers in the normalized stream so
font-rendering regressions are visible in diffs against local Markdown output.

## PACKET PREFACE Highlight Check

For PACKET renderer validation, the same MCP route was used against:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/PREFACE?SHELF=&DT=20260614112503
```

The page title was:

```text
PREFACE "Packet Tutorial"
via IBM BookManager BookServer
```

The source fixture `BOO/packet.script` line 40 contains:

```text
email me at :hp3.wec@bam.moe:ehp3.!
```

The hosted BookServer HTML renders that phrase in the same paragraph as the
preceding text and wraps only `wec@bam.moe` in nested bold and italic tags,
leaving the exclamation point outside the highlighted span.

## PACKET Table Rendering Check

For PACKET table validation, the same MCP route was used against:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/2.4.4?SHELF=&DT=20260614112503
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/3.9?SHELF=&DT=20260614112503
```

The `TABLES` topic identifies the table-bearing topics:

```text
2.4.4, 2.4.5, 3.9, 4.3.1, 6.1, 7.1.1
```

The source fixture `BOO/packet.script` lines 690-716 contains the source
`IPv4 Address Classes` table as `:table cols='* * *'.`, `:tcap.`, `:row.`,
`:c.`, and `:etable.` records. Lines 1932-1937 start the `Linux Packet
Programs` table the same way. BookServer renders the compiled BOO table body
as an HTML table with monospace cells and `<br>` for wrapped fixed-width
lines.

The BookSrv IDB was then checked directly:

```text
Official Readers/BookSrv-Win32/bookmgr.exe.i64
sub_405FC
```

The chapter renderer compares decoded records against `CZ OFF TABLE` at
`0x421af`, `0x43036`, `0x43194`, and `0x433a2`. The matching paths set
table-layout state, call `sub_69440`, and one path emits
`</pre><pre width="132"><!-- table -->`. The same function compares
`CZ OFF ETABLE` at `0x439ad` as an end-of-table/end-of-layout control. This
confirms that those records are structural table layout controls, not visible
paragraph text. Comments were added at those addresses and at `sub_69440`, then
the IDB was saved.

During the first-20 PACKET topic comparison, topic `2.4.4` exposed a raw
projection bug: `SRTBLTBLUNIQ17` carries a 74-character horizontal rule, but
the decoded `CFONT ... ? Class ? Range ? Default Netmask` header segment ends
before the closing separator. BookServer still renders three table columns
(`Class`, `Range`, `Default Netmask`) because the table box width supplies the
right cell boundary. The raw projection therefore records the horizontal rule
width and uses it as the implied final cell boundary for rows that otherwise
have real cell separators.

## PACKET Subtopic Menu Check

For PACKET topic `1.0`, BookServer renders the generated menu as:

```text
Subtopics:
1.1 Original Packet Radio
1.2 Ham Packet Radio
1.3 Bringing it Together
```

The decoded logical stream contains `CMENU`, then `CMITEM 1.1 Original Packet
Radio`, `CMITEM 1.2 Ham Packet Radio`, `CMITEM 1.3 Bringing it Together`, and
`CEMENU`. The BookSrv IDB confirms the split: `sub_405FC` emits the
`Subtopics:` heading at `0x44b0b`, recognizes `CMITEM` at `0x44b8c`, and at
`0x44c56` builds an item `href` from the first token while emitting the
remaining text inside the anchor. Comments were added at those addresses and
the IDB was saved.

## PACKET Footnote Check

For PACKET topic `1.1`, BookServer renders footnote references inline:

```html
technologies.<a href="1.1?DT=20260614112503#FTNFTNUNIQ1"> (1)</a>
interlinked.<a href="1.1?DT=20260614112503#FTNFTNUNIQ2"> (2)</a>
```

The same page renders the footnote bodies at the bottom under anchors named
`FTNFTNUNIQ1` and `FTNFTNUNIQ2`, with an `<hr>` before the first footnote and
`<h5>` around each footnote body. The decoded stream is `CSELECT ... FTNFTNUNIQ1
... technologies. (1)`, followed later by `SRFTNFTNUNIQ1`, `CZ FLOW FN ...`,
and `SREFTN`.

The BookSrv IDB confirms this path in `sub_405FC`: `SRFTN` is recognized at
`0x42356`, the first-footnote `<hr>` is emitted at `0x42388`, the footnote body
`<h5>` is emitted at `0x423a4`, and `CSELECT` tokenization starts at `0x42471`.
Comments were added at those addresses and the IDB was saved.

A full raw render of `packet.boo` found 67 footnote records and 67 unique
`FTNFTNUNIQ...` ids, so the generated ids are unique within this document and
can be reused directly as rendered Markdown anchors.

The same first-20 comparison found that the generated footnote body text in
topic `1.1` decodes with doubled terminal periods immediately before `SREFTN`,
for example `medium access control technique.. SREFTN` and `Internet was
born.. SREFTN`. BookServer emits one visible final period inside each `<h5>`.
The raw projection now treats the second period as the generated footnote
terminator convention only while inside `SRFTN`/`SREFTN` footnote body state.

One first-20 punctuation mismatch remains open: BookServer renders the paragraph
ending `wireless computer network?`, while the current raw projection still
emits `wireless computer network`. The decoded trace around logical record 15
contains a wrapped-line marker before `network` and separator/control markers
before the following `CZ FLOW P`; this needs further reader-code analysis
before changing the decoder.

## PACKET Wrapped-Line Marker Check

For PACKET topic `1.1`, BookServer was fetched through Docker at:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/1.1?SHELF=&DT=20260614112503
```

The hosted HTML renders this as one paragraph:

```html
An  important  milestone  in  the  development of computing networking was
Professor  Norman  Abramson  at  the  University  of   Hawaii's   project:
<B>ALOHAnet.</B>
```

The decoded `packet.boo` logical stream has a paragraph flow record ending in
`networking was`, followed by a plain decoded segment beginning
`$    Professor Norman Abramson...`. Other wrapped lines in the same topic carry
similar marker-plus-indent prefixes inside text fields, for example
`*    or affordable-to-construct`, `!    connect their sites`, `$    system`,
and `-        equipment`. BookServer suppresses those printable marker bytes and
renders the prose without paragraph breaks caused by them.

The BookSrv IDB was checked in `sub_405FC`: the paragraph path emits `<p>` only
when layout state requires it at `0x43865`, recognizes `CZ FLOW P` at
`0x438ae`, and checks paragraph state again at `0x4391a` before emitting a new
`<p>`. This confirms that marker-led wrapped records continue the active
paragraph and that the marker byte is not visible output. Comments were added
at those addresses and the IDB was saved.

## PACKET Labeled Box And Font Continuation Check

For PACKET topic `1.3`, BookServer was fetched through Docker at:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/1.3?SHELF=&DT=20260614112503
```

BookServer renders the `Audio filtering and high-speed packet` detail box as a
preformatted `<!-- lblbox -->` block, not as ordinary paragraph text. The source
fixture confirms this is a source `:lblbox.Audio filtering and high-speed
packet` through `:elblbox.` region in `BOO/packet.script` lines 229-253. The
compiled `packet.boo` stream represents the start and end as `CZ OFF LBLBOX`
and `CZ OFF ELBLBOX`; the visual box title/body is stored between those records.

The same topic has a span-only font record:

```text
CFONT 27 5 3 33 10 3
FM radio through its audio interface; ...
```

BookServer applies that font metadata to the following text line and emphasizes
`audio interface;`. This confirms that `CFONT` records may carry only span
metadata and must be held until the next visible text segment.

The BookSrv IDB contains the expected renderer strings at `0xcfeec`
(`CZ OFF LBLBOX`), `0xcff64` (`CZ OFF ELBLBOX`), and `0xcfc40`/`0xcff10`
(`<!-- lblbox -->` emission formats). Comments were added at those string
anchors and the IDB was saved.

## PACKET XMP, List, Figure, And Index Checks

For PACKET renderer validation on 2026-06-18, the decoded traces were produced
locally with:

```text
./build/bootrace BOO/packet.boo 3.2 --all
./build/bootrace BOO/packet.boo 1.3 --all
./build/bootrace BOO/packet.boo INDEX --all
```

The corresponding hosted BookServer URLs are:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/3.2?SHELF=&DT=20260614112503
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/1.3?SHELF=&DT=20260614112503
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/INDEX?SHELF=&DT=20260614112503
```

Topic `3.2` demonstrates that `CZ OFF XMP` and `CZ OFF EXMP` are literal
example-mode boundaries. Between those controls, decoded visible lines such as
`# name callsign speed paclen window description` and
`radio  WA4XYZ-1 1200  256    7      Real TNC` are rendered by BookServer
inside preformatted output. The same trace shows `CZ FLOW UL 3 3` and empty
`CZ FLOW LI 3 7` controls before `CFONT` and `CSELECT` records; BookServer
keeps those as list structure, so the raw projection must emit empty list
boundaries instead of dropping them and merging the following text into the
previous paragraph.

The same `3.2` trace validates inline font placement inside list items:

```text
CZ FLOW LI 3 7
CFONT 12 9 1 23 4 1                 ?   The  interface  name, ...
```

BookServer renders the visible item as `The` plus highlighted `interface` and
`name`. The active `CZ FLOW` indent column (`7`) is the base for same-line
`CFONT` display columns. Applying the spans after collapsing the doubled
display space after `The` tears the words into fragments.

Topic `3.2` also demonstrates body-embedded subject-index metadata:

```text
CFONT 37 17 P =    To define an AX.25 port, edit /etc/ax25/axports, and, use tabs for
SI Linux AX.25, Configuring Ports, AX.25 ?    everything, not spaces:
```

BookServer does not render the `Linux AX.25, Configuring Ports, AX.25` subject
index term in the body. It renders one paragraph, `To define ... everything,
not spaces:`, before the literal example block. The leading `=` is a decoded
line marker/control boundary, consistent with the `ephwam.dll` logical-record
iterator comparing first text characters against space and `=` before
lowercasing controls.

On 2026-06-18, `QSYSNEWG` topic `1.2` showed that `SI` visible tails are not
limited to explicit `?` or `|` markers:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSNEWG/1.2?DT=19910524085706&SHELF=
```

The hosted page renders the opening prose:

```html
Computers come in many forms and are used for many different things.  Here
are a few ways computers can be used:
```

The local decoded trace carries that text after subject-index controls:

```text
SI computer, description of             Computers come in many forms...
SI computer, ways can be used ?    are a few ways computers can be used:
SI processor           There are many different devices...
```

In `bookmgr.exe`, `BookServer_render_topic_body_html` compares the current
control text against `SI` at `0x41b73` through `0x41b95` and jumps to the
common skip path for the hidden control itself. Visible body rows are still
copied by the surrounding `Scm_Getln` / `Scm_Xoutcpy` loop. In local decoded
text, the hidden-control/body boundary can surface as `?`, as `|`, or as a
wide alignment-space gap; the compatible projection hides the subject term and
keeps the visible tail beginning at the earliest of those boundaries.

Topic `1.3` demonstrates figure image ownership. The trace contains
`SRFIGFIGUNIQ5`, `CZ OFF FIG`, `CSELECT 35 9 PIC1 ... PICTURE 1 Figure 1.
VHF/UHF LMR audio frequency range`, `SREFIG`, and `CZ OFF EFIG`. BookServer
renders the picture as `/bookmgr/pictures/packet.20260614112503.P1.GIF`, then
renders the caption line `Figure 1. VHF/UHF LMR audio frequency range`.
The generated `PICTURE 1` label and the selected `audio fre` placeholder are
not visible caption words around the image.

Topic `1.3` also validates same-line and continuation `CFONT` placement:

```text
CFONT 27 5 3 33 10 3
FM  radio  through  its audio interface; ...
CFONT 3 3 3 7 3 3 11 5 3                    key the radio ...
CFONT 17 3 2 21 6 2 28 3 2 32 4 2 37 7 2 45 3 2 50 7 2 59 3 2 64 13 2
                     packet radio, you cannot use your radio's VOX  control  for  bidirectional
```

BookServer renders whole-word spans for `audio`, `interface;`, `key`, `the`,
`radio`, `you`, `cannot`, `use`, `your`, `radio's`, `VOX`, `control`, `for`,
and `bidirectional`. The span-only first `CFONT` applies to the following
physical text line. The upstream BOO/logical-line source for this behavior is
`ephwam.dll`: `Scm_Getln` delegates to the logical-record iterator
`sub_12217C6`, which appends decompressed segments, records segment starts and
lengths, and inserts reader spaces according to segment continuation markers.
The local renderer must therefore preserve display-column mapping and must not
repair fonts by scoring likely word boundaries after the fact.

Topic `INDEX` demonstrates generated-index termination. The decoded stream
contains generated `CGPSEP` and `CITERM` records through the final
`ROSE 1 2.3` entry, followed by `CENDINDEX`. Local decoded logical records
after `CENDINDEX` contain non-content padding/garbage-looking fragments such as
`have callsign` and `cbacklevel`; BookServer does not render them. The raw
projection therefore treats `CENDINDEX` as the end of the generated index body.

## QS3X36CM Markdown Rendering Validation

The smaller command cross-reference book used for Markdown-rendering regression
work is:

```text
BOO/QS3X36CM.BOO
Application System/400(TM): Programming: System/36 Commands To AS/400 Commands Cross-Reference
Document number: SX41-8209-00
BookServer timestamp: 19910524075122
```

BookServer lazily generates pages on first request. A repeatable crawl is:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/COVER?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/EDITION?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/CONTENTS?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/1.0?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/1.1?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/2.0?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/2.1?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/2.2?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/2.3?DT=19910524075122&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/A.0?DT=19910524075122&SHELF=
```

When the direct shell route is available, this cache-populates the hosted
reader and stores local comparison copies:

```sh
mkdir -p /tmp/qs3x36cm-booksrv
for topic in COVER EDITION CONTENTS 1.0 1.1 2.0 2.1 2.2 2.3 A.0; do
  safe=${topic//./-}
  curl -L "http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/${topic}?DT=19910524075122&SHELF=" \
    -o "/tmp/qs3x36cm-booksrv/${safe}.html"
done
```

The IDA Pro MCP instances used for this pass were:

| Binary | Role |
| --- | --- |
| `ephwam.dll` | Primary BOO/logical-record expansion source. `Scm_Getln` calls the logical-record iterator at `sub_12217C6`; `Scm_Expln` drives topic expansion through `sub_122202E`, `sub_121FFF4`, and `sub_1214753`. |
| `bookmgr.exe` | HTML presentation boundary. Used only to confirm reader-generated navigation such as `Summarize`, topic headings, and fixed-width `<pre width="80">` output. |

On 2026-06-18 the PACKET title-page regression was checked against the attached
BookServer IDB artifacts using `r2` because an interactive IDA MCP tool was not
available in the session. The relevant commands were:

```sh
rabin2 -zz "Official Readers/BookSrv-Win32/bookmgr.exe" | rg "CFONT|CHDLEVEL|TITLE|COVER|<BR>|</B>"
r2 -q -A -c "axt @ 0x000cfb74" -c "axt @ 0x000d03c0" \
  -c "axt @ 0x000d03c8" -c "axt @ 0x000d267c" -c q \
  "Official Readers/BookSrv-Win32/bookmgr.exe"
r2 -q -A -c "s 0x44377" -c "pd 80" -c q \
  "Official Readers/BookSrv-Win32/bookmgr.exe"
r2 -q -A -c "s 0x4f69c" -c "pdr" -c q \
  "Official Readers/BookSrv-Win32/bookmgr.exe"
```

This showed that BookServer's main topic renderer function `0x000405fc`
references `CFONT`, `CHDLEVEL`, `TITLE`, and `COVER`; the branch at
`0x00044377..0x000443c8` tests `TITLE`/`COVER` and sets a title/cover state
flag. The HTML escaping helper `0x0004f69c` maps newline byte `0x0a` to
`<BR>\n`. The hosted reference page:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/packet/TITLE?DT=20260614112503&SHELF=
```

renders the generated title block as bold words with line breaks, then emits
separate paragraph blocks for `Document Number 9963-0413-56`, `January 15,
2026`, and `Evie Cooper`. This confirms that generated title-page `CFONT`
layout columns should not be projected as normal body highlighted-phrase spans
after text collapse.

Topic `2.0` was rechecked against:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QS3X36CM/2.0?DT=19910524075122&SHELF=
/tmp/qs3x36cm-booksrv/2-0.html
```

The hosted BookServer HTML emits the first body text inside `<pre width="80">`
as three physical lines:

```text
System/36 procedures, control commands, and OCL statements are listed
alphabetically, with cross-references to AS/400* commands, beginning on
the following pages:
```

`./build/bootrace BOO/QS3X36CM.BOO 2.0 --all` shows those lines came from the
topic-header `ST` segment after the TOC title, not from a separate `CZ FLOW P`
record. The decoded segment contains a wide display-padding run between
`listed` and `alphabetically`, then `?    ` before `the following pages:`.
`ephwam.dll` `Scm_Expln` reaches `sub_121FFF4` and `sub_1214753` during topic
expansion. `sub_121FFF4` calls `sub_121E7BB` to advance to the next positive
typed item and then calls `sub_121EEE1` to expand that one item, while
`sub_1214753` calls `sub_12144E6` to walk the expanded display/link structures.
This means an `ST` body must be bounded at the next typed item such as
`CSELECT`, `SR`, or `SRTBL`; anchors and tables are not part of the `ST` body.
For topic `2.0`, the bounded `ST` body is a complete colon-terminated
introductory paragraph and the following typed items are the three `CSELECT`
page-reference links. For topic `1.0`, the following `CSELECT` continues the
sentence, and for topic `2.1`, `SRSPTPROC` anchors the next display line before
`SRTBLTBLUNIQ1`; those are not the same case. The projection rule is therefore
to split `ST` at the verified TOC title, stop at the upstream typed-item
boundary, and preserve reflow-off display-line separators only for complete
`ST` intro paragraphs before the page-reference `CSELECT` items.

Local validation commands:

```sh
./build/bootrace BOO/QS3X36CM.BOO 1.0 --all
./build/bootrace BOO/QS3X36CM.BOO 1.1 --all
./build/bootrace BOO/QS3X36CM.BOO 2.0 --all
./build/boorender BOO/QS3X36CM.BOO 2.0 --raw
./build/boorender BOO/QS3X36CM.BOO 2.0 --md
./build/boorender BOO/QS3X36CM.BOO 2.1 --raw
./build/boorender BOO/QS3X36CM.BOO A.0 --md
ctest --test-dir build --output-on-failure
```

## QSYSNEWG Visual-Box CFONT Root Cause

On 2026-06-18, `QSYSNEWG` topic `1.0` from the hosted BookServer cache was used
to root-cause the torn Markdown emphasis reported in `todo.md`. The cached
BookServer page:

```text
BookServerCache/QSYSNEWG/1_0.html
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSNEWG/1.0?DT=19910524085706&SHELF=
```

renders the opening callout as `<pre width="80">` with bold whole words:

```text
___ In a Hurry? ...
| This chapter contains ...
| Official Introductory Chapter ...
```

`./build/bootrace BOO/QSYSNEWG.BOO 1.0 --all` shows the source of the callout is
logical record 18, not a normal paragraph:

```text
CFONT 8 2 2 11 1 2 13 6 2      ???? In a Hurry? ...
CFONT 5 8 2 14 12 2 27 7 2 ... ? Official Introductory Chapter
```

The old renderer collapsed the visual border/separator bytes before applying
the display-column spans, producing fragments such as `**Hu**r**r**y` and
`Of**ficial I**n**troductory C**h**apter**`. The fix belongs in GML
normalization: detect multi-span `CFONT` records with visual border runs,
recover the visual-box rows, apply spans only when they match consecutive
leading words, and then pass ordinary normalized GML to the Markdown layer.

r2 was used against the BookServer binary to confirm this path is the same
BookServer presentation boundary already documented for other rendering work:

```sh
rabin2 -zz "Official Readers/BookSrv-Win32/bookmgr.exe" | rg "CFONT|<pre width|</B>"
r2 -q -A -c "axt @ 0x000cfb74" -c "axt @ 0x000cfbe4" -c q \
  "Official Readers/BookSrv-Win32/bookmgr.exe"
```

The bounded string/xref pass shows `CFONT` at `0x000cfb74` and
`<pre width="80">` at `0x000cfbe4` are both referenced from the main topic
renderer function `fcn.000405fc`, matching the earlier BookServer IDB notes.

## QSYSNEWG Figure Text And Fixed-Row CFONT

On 2026-06-18, topics `2.0` and `2.1` were fetched from the hosted BookServer
and cached for local comparison:

```text
BookServerCache/QSYSNEWG/2_0.html
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSNEWG/2.0?DT=19910524085706&SHELF=

BookServerCache/QSYSNEWG/2_1.html
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSNEWG/2.1?DT=19910524085706&SHELF=
```

`./build/bootrace BOO/QSYSNEWG.BOO 2.1 --all` showed that the missing Sign On
screen is not a picture resource. It is fixed text trailing the figure start
control:

```text
SRFIGFIGUNIQ13 ? ... Sign On ... System  . . . . . :   XXXXXXXX ...
```

The local decoder was treating all trailing text as the figure id, producing a
giant invalid anchor and no screen. BookServer renders `<a name="FIGFIGUNIQ13">`
and then the fixed-width text display. The compatible interpretation is:
`FIGUNIQ13` is the id token; the rest of the segment is figure body text.

The same trace showed `SREFIG` can carry trailing body text:

```text
SREFIG       You can ignore the information in the upper right corner of the display.
SREFIG           Before you can use the AS/400 system you must sign on ...
```

BookServer closes the figure and then renders that text as normal topic body.
The old decoder dropped the trailing text, which caused topic `2.0` to start in
the middle of the paragraph after the image.

The fixed-row font spans in these topics use the same renderer path as the
earlier visual-box issue. On a follow-up pass, the IDA MCP instance was switched
to `Official Readers/BookSrv-Win32/bookmgr.exe.i64` and `sub_405FC` was renamed
to `BookServer_render_topic_body_html`. Targeted IDB xrefs show:

- `<pre width="80">` at `0x000cfbe4` is emitted by
  `BookServer_render_topic_body_html` for fixed-width topic/figure/labeled-box
  output. The figure path uses the format string `%s<!-- figure -->\n`.
- The same function loops through `Scm_Getln` at `0x4545f` and writes the
  buffered line at `0x454d1`/`sub_7B8E0`; this supports preserving fixed rows
  from the decoded line stream rather than reconstructing a hardcoded screen.
- `CFONT` at `0x000cfb74` is recognized in the same function. At `0x4244b` it
  calls `sub_4920A`, which tokenizes repeated `CFONT` triples into an array.
- `CZ OFF EFIG` at `0x000cff58` clears the figure-state flag at `0x43a9d`,
  confirming that figure body bytes before that marker remain inside the
  fixed-width figure/pre block.

Specific span evidence:

```text
2.1: CFONT 28 4 2 33 2 2      | After a few seconds, the Sign On display comes on.
2.0: CFONT 52 4 2 57 4 2    | station ... unique user name which
2.0: CFONT 31 8 2    | identifies ... secret password ...
2.0: CFONT 7 8 1 16 8 1 25 3 1 29 8 1 53 10 1 64 5 1
                  Security Concepts and Planning manual and the Operator's Guide.
```

BookServer renders `Sign`/`On`, `user`/`name`, `password`, and the manual-title
words as whole highlighted words. Applying offsets after Markdown/plain-text
collapse tears the spans. The fix maps pipe-led rows in the fixed display-row
coordinate space and handles the highly indented manual-title row as ordered
whole-word spans in fixed-layout text.

The Sign On display in hosted topic `2.1` is an exact fixed-row screen. After
removing the anchor tag, BookServer emits 28 rows from the top rule through the
bottom rule; every row is 87 characters wide, consisting of a three- or
four-column left margin plus an 82-column display payload and border. Local
validation now compares the generated fenced text rows against those hosted
rows and checks the same row count and width.

One negative control is `QSYSNEWG.BOO` topic `F.1`: it also appears inside
`SRFIG`, but the decoded stream enters `SRTBL` before the long `?` table rows.
The BookServer renderer handles table scope separately from fixed screen output,
so local normalization must let table parsing take precedence over figure text
recovery while `SRTBL` is active.

On the same book, topic `FRONT_1` exposed a separate heading/body split issue.
`./build/bootrace BOO/QSYSNEWG.BOO FRONT_1 --all` showed logical record 13 as:

```text
SRHDRNOTICES ? ST  Notices        References in this publication to IBM products, programs, or services do ...
```

Normalization projected this as an anchor followed by a heading carrying body
text:

```text
:anchor id='HDRNOTICES'.
:h1.Notices References in this publication to IBM products, programs, or services do ...
```

The hosted `FRONT_1` page renders:

```html
<a name="HDRNOTICES"><H1> FRONT_1   Notices</H1></a>
<pre width="80"><!-- * -->
   References in this publication to IBM products, programs, or services do
```

The topic renderer then emitted the TOC-derived `FRONT_1 Notices` heading and
treated the original heading as a duplicate. The old duplicate-heading cleanup
deleted the whole `:h1` record, losing the first paragraph and making the topic
start at logical record 14 (`business operations...`). The compatible behavior
is to split the first title record after any leading anchors, keep `Notices` as
the heading title, and preserve the trailing `References...` text plus
continuation records as reflow-off fixed lines.

This is not a topic-name special case. `QS3X36CM.BOO` shows the same
`CHDLEVEL`/`ST` title-plus-body pattern, but its `ST` bodies can be followed by
typed inline controls: topic `1.0` continues with `CSELECT`, topic `1.1` with
`CFONT`, and topic `2.0` has a colon-terminated intro before `CSELECT` page
references. The compatible local split is therefore structural: plain
reflow-off `ST` body text can become fixed lines, while `CSELECT`/`CFONT`
continuations must stay in the structured path so links and font spans survive.

On 2026-06-18, `QSYSNEWG` topic `PREFACE` exposed the same structural path with
a visual marker attached to the title control:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSNEWG/PREFACE?DT=19910524085706&SHELF=
```

Hosted BookServer emits:

```html
<a name="HDRABOUT"><h1>| PREFACE   About This Guide</h1></a>
<pre width="80"><!-- * -->
 | This guide contains a very basic approach...
```

The downloaded HTML stores the simple-list marker as byte `0xb0` in the
ISO-8859-1 page:

```text
20 7c 20 b0 20 20 20 53 69 67 6e ...
```

The local decoded trace for logical record 15 shows:

```text
SRHDRABOUT ? ST| About This Guide ... | following basic tasks: | ?   Sign on ...
```

The first local failure treated `ST|...` as payload of the `SRHDRABOUT` anchor,
which leaked `ST|` and visual bars into Markdown. The structural fix is to
recognize `ST|` as `ST` followed by a reflow-off row marker, and to require
valid boundaries on both sides of following typed controls so prose words like
`Sign` are not mistaken for `SI`.

IDA MCP evidence came from both loaded BookServer binaries:

- In `bookmgr.exe`, `BookServer_render_topic_body_html` references
  `<pre width="80">` at `0x45239` and loops over `Scm_Getln` at `0x452ff` /
  `0x4541c`. At `0x45328` it calls imported `Scm_Xoutcpy` to copy fixed-row
  text into the output buffer.
- The same `bookmgr.exe` function does not close the pre block when fixed-row
  text crosses into `CFONT`/`CSELECT` continuation records. The first `</pre>`
  output path is guarded later at `0x4548e`, and a final cleanup path at
  `0x45572` closes any still-active pre block. This matches hosted `QSYSNEWG`
  `PREFACE`, where the `Publications Guide` italics and `Bibliography` links
  remain inside the `<pre width="80">` block.
- In `ephwam.dll`, `Scm_Xoutcpy` (`0x121af51`) is a wrapper around
  `sub_121AC63`. That routine walks 16-bit BookManager character words, calls
  `sub_121C31C` to select/load the current output table, and writes the mapped
  low byte when the mapped value is `<= 0xff`.

Therefore the visible `0xb0` simple-list glyph is not a Markdown-only invention.
It is produced by the upstream output-copy translation path for a marker that
the current lossy decoded trace still displays as a placeholder. In the local
projection, the matching structural pattern is a single placeholder immediately
after a leading visual row marker in a colon-introduced simple-list block
(`| ?   text`). Once this marker starts a simple list, following rows in the
same logical-record body remain list items until the synthetic logical-record
boundary; the next paragraph is not bullet-prefixed.

## GG24-395 And XWEBDEMO Image Selector Recheck

On 2026-08-25, the complete cached BookServer captures were rerun after the
image-selector implementation change:

```sh
python3 tools/bookserver_book_audit.py BOO/GG24-395.boo \
  --book-id GG24-395 --timestamp 19941215160749 \
  --output /tmp/geist-books20-audits/GG24-395 --jobs 4 --no-fetch
python3 tools/bookserver_book_audit.py BOO/XWEBDEMO.boo \
  --book-id XWEBDEMO --timestamp 19970423182524 \
  --output /tmp/geist-books20-audits/XWEBDEMO --jobs 4 --no-fetch
```

All 226 `GG24-395` topics and all 13 `XWEBDEMO` topics rendered successfully.
The focused evidence was `GG24-395` topics `3.3.7`, `3.3.11`, and `3.3.15`
(logical records 528, 581--582, and 635), plus `XWEBDEMO` topics `1.0`,
`1.4.1`, `1.4.2`, `1.4.3`, `1.4.4`, and `FIGURES`. The comparison confirmed
that picture-bearing `SRTBL` layouts retain prose across logical-record and
`SRETBL` boundaries, and that `LNK` alternatives retain their image, HTTP,
FTP, video, and audio targets. A separate residual XWEBDEMO presentation issue
tracks title/body layout controls that are independent of selector targets.

## GG24-4302-00 CSELECT Column And Delimiter Check

The IBM redbook *IMS 5.1 Guide* (`GG24-4302-00`, BOO version 1.2) was checked
at the exact hosted topic URL:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/GG24-4302-00/NOTICES
```

The topic is `NOTICES`; its cross-reference target is topic `FRONT_1`, anchor
`HDRNOTICES`. BookServer places `"Special Notices" in` inside the first anchor
and `topic FRONT_1` inside the second anchor. The final period is outside the
second anchor. The local decoded logical record contains:

```text
CSELECT 43 30 HDRNOTICES ... ? to read the general information under "Special Notices" in
CSELECT 5 13 HDRNOTICES ... ? topic FRONT_1.
```

The record is physical page 121, record 0. Its compact-record length byte is at
BOO offset `0x79004`, its 83-byte payload begins at `0x79005`, and the payload
starts `00 24 01 04 01 05 01 03 04 08 01 09 03 0b 13 06 ...`. Raw topic
tracing was used to retain the decoded fixed-layout fragments; the hosted page
was fetched through the Docker fetch MCP as required for this CGI.

The BookSrv IDB was then followed from `sub_405FC`, which recognizes and queues
`CSELECT` at `0x42471`, into the renderer at `0x4b483`. The latter parses the
first integer as an absolute zero-based column and the second as a cell count,
copies the intervening ordinary cells from the complete display line, emits the
selected range, and resumes at its end. It was renamed
`RenderDisplayLineObjectsAndSelections` with `ida_name.set_name`, given a
repeatable behavior comment, and the IDB was saved.

Regression examples were selected from *Packet Tutorial* (`PACKET`). The
hosted topic below confirms that `CSELECT 16 4 FTNFTNUNIQ1 ... technologies.
(1)` links only `(1)`:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/PACKET/1.1
```

Additional local PACKET controls include column 58/length 4 selecting `(8)`
and a column 60/length 4 selector whose display text follows in a `CFONT`
record and selects `(9)`. Together with the fixed-box `NOTICES` examples, these
cases distinguish absolute display spans from a trailing-character heuristic.

The complete PACKET book was then exported to a temporary directory and
compared with the checked-in render: 124 topics and nine PNG resources were
produced. All nine resource hashes were unchanged. The selector correction
changed 36 Markdown files because the previous suffix heuristic had omitted or
misplaced footnote callouts across the book. A corpus scan found all 67 unique
`FTNFTNUNIQ` reference targets, zero links whose visible text was the target id,
and zero footnote links around non-callout prose.

The regression pass also exposed two flattened-line forms not represented by
the initial examples. Generated list or definition prefixes can be absent from
the visible fragment but still count toward the native absolute column. A run
of `?` placeholder bytes followed by blanks encodes that suppressed structural
prefix: discard the run and one guard blank, retaining the other blanks as the
continuation margin. PACKET topic `5.1.3.1` has
`CSELECT 9 5 FTNFTNUNIQ60`, 23 placeholders, eight blanks, and `to (47)`;
topic `6.2.1` has `CSELECT 33 5 FTNFTNUNIQ69`, 23 placeholders, 23 blanks, and
`connections (55)`. Direct continuation fragments can omit a generated prefix
too, as in topic `8.1`, where `CSELECT 63 5 FTNFTNUNIQ83 ... radio (67)` needs
seven suppressed list cells. These cases were added as focused synthetic tests
and were included in the final whole-book scan.

## GG24-4302-00 Abstract ST Body

The hosted comparison topic was fetched through the Docker fetch MCP at:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/GG24-4302-00/ABSTRACT
```

BookServer emits the complete abstract in `<pre width="80">`, with paragraph
breaks and a final `(217 pages)` line. Local `bootrace` identifies the topic as
logical records 6 through 7; all visible content is in record 6 after
`ST  Abstract .`.

The attached IDBs were switched from `bookmgr.exe` (port 13337) to
`ephwam.dll` (port 13338) to follow the actual read path. In the DLL,
`Scm_Getln` (`0x1221aed`) calls `BooReadNextLogicalRecord` (`0x12217c6`), which
loads descriptors through `BooExpandLogicalRecordTokens` (`0x121eee1`) and
resolves token text through `BooResolveTokenTextRecord` (`0x1218250`). In the
BookServer executable, the topic renderer at `0x405fc` calls `Scm_Getln` at
`0x40b19`, translates the result with `Scm_Xoutcpy` at `0x40b63`, recognizes
the `ST` control at `0x40d7a`/`0x40da9`, and replaces calculated display-column
spaces with carriage-return row boundaries at `0x40df4` and `0x40e60` before
the fixed body is emitted.

The local failure was earlier in the projection: its fallback `ST` search
accepted the final two letters of `CSOURCEFN 4302ABST` as the control and the
structural-topic attachment path then discarded the real `ST` suffix. The
repair requires a token boundary before fallback `ST`, consumes the standalone
title/body dot, and attaches the remaining fixed rows after `:abstract.`.

## GG24-4302-00 FIGURES fixed-row list

The hosted comparison was fetched through Docker MCP from:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/GG24-4302-00/FIGURES
```

The response contains `<pre width="80"><!-- * -->` and a separate anchor row
for each figure selection. Local `bootrace` shows `CHDLEVEL :FIGLIST`,
`ST Figures`, then `CSELECT` rows across logical records 15--19. Some rows
continue in a later segment: figure 12 starts with `CSELECT 3 72 FIGRMFWL01`
at record 15/segment 24 and its visible text is record 16/segment 0.

The actual read path was verified in the attached IDBs by switching to
`ephwam.dll` (port 13338): `Scm_Getln` (`0x1221aed`) calls
`BooReadNextLogicalRecord` (`0x12217c6`), which expands tokens through
`BooExpandLogicalRecordTokens` (`0x121eee1`) and
`BooResolveTokenTextRecord` (`0x1218250`). The BookServer renderer in
`bookmgr.exe` (`sub_405FC`) consumes each result and its fixed-body loop emits
the `<pre>` rows. The defect was in our later projection: `render_select_gml`
returned `:pinline.` for every selection and `append_rendered_gml_line` merged
adjacent inline fragments. FIGLIST now converts those selections, including
deferred continuations, to independent `:p.` blocks while leaving ordinary
prose coalescing unchanged.

The same structural path applies to `TABLES`:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/GG24-4302-00/TABLES
```

Local `bootrace` identifies `CHDLEVEL :TLIST`, `ST Tables`, and 15 `CSELECT`
rows in logical records 19--21. BookServer emits one row per anchor in its
fixed `<pre>` block. This confirmed that the correct abstraction is a shared
fixed-selection-list state for both `FIGLIST` and `TLIST`, not a FIGURES-only
special case.

## GG24-4302-00 FRONT_1 table truncation

Hosted BookServer URL:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/GG24-4302-00/FRONT_1
```

The local topic spans logical records 21--24. Its trademark table begins in
record 23 with `SRTBLTBLUNIQ1` and the first row in the same decoded segment,
then closes with `SRETBL`; a second `SRETBL` boundary follows immediately.
The previous renderer treated the entire segment as the table caption, so the
table body disappeared and Markdown stopped at a reference-like table shell.
The fix splits `SRTBL` at its first row separator, parses the row (including
legacy spacing when separator markers have already been materialized), and
ignores a duplicate close boundary. This restores the missing notice text and
table content without changing ordinary table captions.

## SC41-485 and SC31-711 definition/form verification

The definition-list and worksheet comparisons used these hosted topics:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/SC41-485/1.2.5?DT=19951003131222&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/SC31-711/2.4.1?DT=19941010174546&SHELF=
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/SC31-711/2.4.8?DT=19941010174546&SHELF=
```

After the parser changes, the complete cached comparison was reproduced with:

```sh
python3 tools/bookserver_book_audit.py BOO/SC41-485.boo --book-id SC41-485 --timestamp 19951003131222 --output /tmp/geist-books20-audits/SC41-485 --jobs 4 --no-fetch
python3 tools/bookserver_book_audit.py BOO/SC31-711.boo --book-id SC31-711 --timestamp 19941010174546 --output /tmp/geist-books20-audits/SC31-711 --jobs 4 --no-fetch
```

All 36 and 82 cached reference pages were available. The heuristic flag counts
fell to 21 and 47 respectively; those flags remain triage hints rather than
pass/fail results because BookServer's navigation and fixed `<pre>` wrappers
intentionally differ from Markdown structure.

## GG24-4302-00 edition notice verification

The book-specific edition data was compared with:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/GG24-4302-00/EDITION?DT=19950308184737&SHELF=
```

The local raw topic retains February 1995, IMS/ESA Version 5 Release 1, the
mailing-address prose, and the 1995 copyright. The former Markdown helper
discarded most of those records and substituted the May 1991 values from
`QS3X36CM.BOO`. The corrected path extracts the heading, applicability text,
copyright, and government-rights suffix from each book's decoded records.

Afterward, a fresh live audit fetched and compared all 229 GG24 topics:

```sh
python3 tools/bookserver_book_audit.py BOO/GG24-4302-00.boo --book-id GG24-4302-00 --timestamp 19950308184737 --output /tmp/geist-gg24-4302-audit --jobs 4 --timeout 30
```

All 229 reference fetches succeeded and 33 topics retained heuristic flags.

The fixed-row paragraph reconstruction was subsequently verified with a new
live, full-book comparison:

```sh
python3 tools/bookserver_book_audit.py BOO/GG24-4302-00.boo --book-id GG24-4302-00 --timestamp 19950308184737 --output /tmp/geist-GG24-post38-final --jobs 4
```

All 229 reference topics fetched successfully.  `EDITION` had no heuristic
flag and a 0.9610 normalized-text ratio; the diff showed the same five notice
body paragraphs as BookServer.  The audit reported 31 flagged topics overall,
which were triaged separately before closing the BOO-specific issue.

## SC31-605 action-code table verification

The three-column action-code grid was compared with:

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/SC31-605/2.1?DT=19911015203151&SHELF=
```

`bootrace BOO/SC31-605.boo 2.1 --segments` shows the styled two-line heading in
logical record 48 and the 74-column data rows continuing through record 57.
The first rows of later records include action codes `05`, `0F`, `19`, `23`,
`63`, `6F`, and `7B`. The local renderer now reconstructs the same three
columns and retains those boundary rows.

The complete cached comparison was replayed with:

```sh
python3 tools/bookserver_book_audit.py BOO/SC31-605.boo --book-id SC31-605 --timestamp 19911015203151 --output /tmp/geist-books20-audits/SC31-605 --jobs 4 --no-fetch
```

All 110 cached pages rendered; 91 retained heuristic flags. Review of those
flags separated the chapter 3 event/qualifier grid family from this action-code
layout rather than treating the audit score as a functional failure.

The chapter 3 event/qualifier correction was then checked against a fresh live
full-book capture:

```sh
python3 tools/bookserver_book_audit.py BOO/SC31-605.boo --book-id SC31-605 --timestamp 19911015203151 --output /tmp/geist-SC31-605-post39-final --jobs 4
```

All 110 hosted topics fetched successfully.  The audit still reports 91
heuristic flags because BookServer exposes the grids as emphasized fixed-width
`pre` blocks whereas libgeist emits semantic Markdown tables.  Direct diffs for
topics `3.2`, `3.5`, and `3.8` confirm the same event codes, one row per code,
four complete headers, and the same wrapped qualifier-cell contents.  Every
flagged page was reviewed before closing the book-specific defect: chapter 2's
table-versus-`pre` differences are the already-corrected action grids, chapter
3's flags are the semantic-table-versus-`pre` representation difference, and
the remaining substantive fixed-layout index/glossary residuals belong to
issue 40.  No independent new defect was found.

## Cross-book CFONT regression verification

Issue 9's ordinary-flow span fixes were checked against these hosted books and
cached captures:

```sh
python3 tools/bookserver_book_audit.py BOO/GG24-4302-00.boo --book-id GG24-4302-00 --timestamp 19950308184737 --output /tmp/geist-gg24-4302-audit --jobs 4 --no-fetch
python3 tools/bookserver_book_audit.py BOO/SC41-485.boo --book-id SC41-485 --timestamp 19951003131222 --output /tmp/geist-books20-audits/SC41-485 --jobs 4 --no-fetch
python3 tools/bookserver_book_audit.py BOO/SC31-711.boo --book-id SC31-711 --timestamp 19941010174546 --output /tmp/geist-books20-audits/SC31-711 --jobs 4 --no-fetch
python3 tools/bookserver_book_audit.py BOO/SH12-565.boo --book-id SH12-565 --timestamp 19941206115523 --output /tmp/geist-books20-audits/SH12-565 --jobs 4 --no-fetch
python3 tools/bookserver_book_audit.py BOO/SC34-425.boo --book-id SC34-425 --timestamp 19921112160049 --output /tmp/geist-books20-audits/SC34-425 --jobs 4 --no-fetch
python3 tools/bookserver_book_audit.py BOO/SC24-546.boo --book-id SC24-546 --timestamp 19940323131240 --output /tmp/geist-books20-audits/SC24-546 --jobs 4 --no-fetch
python3 tools/bookserver_book_audit.py BOO/PRG1SORT.boo --book-id PRG1SORT --timestamp 19900829171904 --output /tmp/geist-books20-audits/PRG1SORT --jobs 4 --no-fetch
```

The targeted comparisons used GG24 topics `1.0`, `4.2.2`, `4.2.4`, `4.2.5`,
`7.3`, `8.5.5`, and `9.4.7`; SC41 topic `1.2.2`; SC31 topics `3.3`, `4.1.1`,
`4.1.3`, `4.2.2`, and `4.4`; SH12 topic `APPENDIX1.8`; SC34 topics `1.9.2`,
`2.2.8`, `APPENDIX1.5.3`, `APPENDIX1.5.4`, and `GLOSSARY`; SC24 topics
`2.1.3`, `2.2.3`, `2.4.3`, and `6.2.6`; and PRG1SORT topic `C.1`.

The ordinary-flow fixes restore complete GG24 product/command/message tokens,
SC41 definition terms, SC31 message labels, SH12 numeric labels and `triplets`,
and SC24's `any` emphasis while retaining valid UTF-8.  The audits also show a
separate residual: SC34 message catalogs and PRG1SORT collating tables are
fixed/preformatted structures whose `CFONT` controls currently pass through a
table or fixed-row path before ordinary CFONT dispatch.  Their remaining
over-emphasis and row collapse must be tracked as structural rendering work,
not as another byte/character-offset repair.

## XWEBDEMO presentation-control recheck

Issue 36 was verified against a fresh 13-topic BookServer fetch after the
decoded-presentation cleanup:

```sh
python3 tools/bookserver_book_audit.py BOO/XWEBDEMO.boo \
  --book-id XWEBDEMO --timestamp 19970423182524 \
  --output /tmp/geist-XWEBDEMO-layout-audit-post36-final --jobs 4
```

All 13 reference pages fetched.  The targeted `TITLE`, `FIGURES`, `1.0`,
`1.1`, `1.3`, `1.4.1`, and `1.4.3` comparisons no longer contain `c.sp`, empty
selector alternatives, selector-kind names, slash/heading row sentinels, or
selector metadata in TOC titles.  Eight pages retain heuristic differences.
The substantive residuals in `1.1` and `1.4` are fixed-row carry-over and
multi-record style assembly problems in the structural path tracked by issue
40, rather than residual presentation-token leakage.

## SC31-711 SRMSG catalog recheck

The SRMSG reconstruction work was checked with a fresh complete fetch:

```sh
python3 tools/bookserver_book_audit.py BOO/SC31-711.boo \
  --book-id SC31-711 --timestamp 19941010174546 \
  --output /tmp/geist-SC31-711-post37-final --jobs 4
```

All 82 topics fetched and rendered; 40 retained heuristic flags.  Topics
`4.1.1`, `4.2.1`, `4.2.2`, `4.3.2`, `4.3.4`, and `4.4` now suppress literal
`SRMSG`, reconstruct stable message anchors/rows, remove duplicate catalog
introductions, and do not expose the 6611 catalog as an XMP code block.  The
full recheck also confirms unresolved semantic carry-over in `3.3`
(`action`, `application`, `connection`) and `4.3.4` (`can`).  Those require the
shared fixed-row ownership model tracked by issue 40, so issue 37 must remain
open until that structural dependency is resolved.
