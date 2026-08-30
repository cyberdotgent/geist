# Fixed-layout regions render verbatim (2026-08-30)

Workflow note for the slice that retired Geist's Markdown-table output for
fixed-layout regions. The normative format facts are in
`Format/markup.md` §"`cz OFF TABLE` is the only mark of a genuine table"; this
note records how the evidence was gathered so it can be repeated.

## Hosted book identity

The hosted BookServer names a book by its own short name, not by its document
number, and several corpus books resolve to a different short name and a
different DT than a name-based guess produces. The reliable procedure is:

1. Fetch the whole library listing:
   `curl -s 'http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS?Collection=Z:\var\www\html&BKCTITLE=IBM+SoftCopy+Library'`
   and parse each row into `(short name, DT, document number)`.
2. Read the document number Geist prints in `README.md` for the book
   (`Document number: \`SC09-1384-00\``) and match on that, preferring the row
   whose short name is *not* the document number.

The mapping this produced for the 32 catalogued corpus books (all confirmed by
comparing the hosted `<title>` and the first topic's heading):

| BOO | hosted name | DT |
| --- | --- | --- |
| ACPZMST1 | ACPZMST1 | 19920319123146 |
| DREICMST | DREICMST | 19911219125856 |
| FA1PLMM0 | FA1PLMM0 | 19910927114801 |
| GC23-046 | GIMJMST | 19910711093248 |
| GC28-183 | GC28-1830-02 | 19930625102617 |
| GG24-395 | GG24-3950-01 | 19941215160749 |
| GG24-4302-00 | GG24-4302-00 | 19950308184737 |
| GX27-3999-00 | GX27-3999-00 | 19950730184057 |
| IBMMMSTR | IBMMMSTR | 19911004151140 |
| IEAC6MST | IEAC6MST | 19920124000100 |
| ITPPIBOK | ITPPIBOK | 19910628074854 |
| N2AH1MST | N2AH1MST | 19910329000100 |
| OFCUSEOV | OFCUSEOV | 19900805103816 |
| PRG1SORT | PRG1SORT | 19900829171904 |
| QS3X36CM | QS3X36CM | 19910524075122 |
| QSYSINFO | QSYSINFO | 19910524120827 |
| QSYSNEWG | QSYSNEWG | 19910524085706 |
| SC09-138 | EDCGUIDE | 19910321130500 |
| SC09-2417-00 | SC09-2417-00 | 19961114175628 |
| SC24-546 | SC24-5466-04 | 19940323131240 |
| SC24-5520-00 | LACNMST | 19911011135123 |
| SC24-5527-02 | SC24-5527-02 | 19921218151459 |
| SC26-457 | IGGV4330 | 19911220230217 |
| SC28-1881-05 | SC28-1881-05 | 19930326130533 |
| SC31-605 | SC31-6055-01 | 19911015203151 |
| SC31-711 | SC31-7111-00 | 19941010174546 |
| SC33-033 | SC33-0333-00 | 19930422134757 |
| SC34-425 | SC34-4254-03 | 19921112160049 |
| SC41-485 | SC41-4853-00 | 19951003131222 |
| SG24-204 | SG24-2047-00 | 19971218054640 |
| SH12-565 | SH12-5657-04 | 19941206115523 |
| SH20-918 | IA6RMSTR | 19910520154851 |
| packet | packet | 20260614112503 |

`SC24-5520-00`, `SC24-5527-02`, `SC28-1881-05` and `packet` *are* catalogued,
contrary to an earlier note; `SC24-5520-00` is served as `LACNMST`.

## Reading hosted's markup

A topic page wraps its whole body in `<pre width="80">` or `<pre width="132">`,
and closes and reopens it around each object region, so a page carries several
`<pre>` elements. **The opening tag always carries a `width` attribute**, so a
`<pre>` regex without `[^>]*` matches nothing -- that mistake made an early
sweep report "hosted serves no `<pre>` anywhere", which is the opposite of the
truth. Regions are additionally named by an HTML comment right after the tag:
`<!-- * -->`, `<!-- table -->`, `<!-- figure -->`, `<!-- lblbox -->`.

Some books (`OFCUSEOV`) are served with CRLF line endings; strip the `\r`
before comparing.

## Line-for-line comparison against hosted

With Geist's fixed-layout regions rendered as fenced verbatim blocks, our
output can be compared against hosted's `<pre>` content directly: strip the
tags, decode `&lt;`/`&gt;`/`&amp;`/`&quot;`, split on newlines, right-trim, and
look for our block's line sequence as a contiguous run of hosted's lines.

Result over the 765 topics (30 books) whose Markdown table this slice retired:
**1,004 of 1,015 blocks (98.9%) match hosted line for line**, and 755 of 765
topics (98.7%) have every block exact. The eleven that differ are enumerated in
the slice's report; none loses content.

Pre-existing verbatim blocks (drawn boxes and drawn figures, which the prose
and figure families already emitted) match at 33.9% exactly, and a further
44.6% match once the region's own left margin is restored -- `ProseBlockIR`
removes the common indent of a preformatted region, and hosted keeps it. That
is a one-class, measured gap and the obvious next slice.

## Difference classes still open

- The region's left margin, above (prose/figure families only).
- The per-book display-translation table: hosted maps `→`/`←`/`↑`/`↓` and the
  `°` bullet through the book's own table; Geist prints the source glyph.
- The reflow-off revision bar: hosted prints ` | ` in the leftmost columns of a
  changed line (`SC26-457` `2.1`/`2.5`); Geist claims it as structural.
- A fenced Markdown block cannot carry a link, so a `CSELECT` inside a verbatim
  region keeps hosted's text and loses its anchor. The recovered link stays in
  `ProseTopicIR::table_links`, so nothing is lost from the pipeline.
- `SC26-457` `FRONT_1` differs in content from hosted's build of the same
  document number (`ESCON | RT` there, `ESCON | System/360` here): the
  BOO in `BOO/` is a different build, not a rendering difference.
