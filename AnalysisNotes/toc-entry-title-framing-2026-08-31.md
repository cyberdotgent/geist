# TOC Entry Titles End At Their Display Line — 2026-08-31

Source trail for issue #86 ("68 topics have a heading that disagrees with their
own TOC entry, in two opposite directions").

## Question

Over the 34-fixture corpus, 69 topics have a `#` heading that is a proper
prefix of their README table-of-contents entry (measured with whitespace
normalised: 6,668 equal, 69 prefix, 625 otherwise different, 7,362 compared).
The issue described these as splitting two ways — genuinely wrapped titles
where the TOC is right, versus stray markers where the heading is right — and
asked which projection to trust.

## Hosted arbitration

Every one of the 69 was fetched twice from the hosted BookServer: the topic
page for its heading, and the book's `CONTENTS` page for its TOC entry.

```
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/<basename>/<topic>?DT=<dt>
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/<basename>/CONTENTS?DT=<dt>
```

Plain `curl` works; responses are latin-1 with CRLF. The shelf id is the
fixture's **file basename**, not its document number, and the `DT` is the
fixture's own build timestamp as reported by `booinfo`. The `<base href>` of
each response was checked against the topic requested: 69/69 served the topic
asked for, 0 mis-served.

Result:

| | topics |
| --- | ---: |
| geist matches hosted on **both** the heading and the TOC entry | 48 |
| heading matches hosted; geist's TOC entry has gained a token | 20 |
| TOC entry matches hosted; geist's heading is truncated | 1 |

The first group is not a defect: hosted BookServer itself truncates the
heading at the topic's `ST` display row and carries the whole title in the
contents. `GC23-0469-01` (shelf `GC23-046`, DT `19920330095121`) topic `A.0`:

```html
<a name="HDRAINSTL"><H1>| A.0   Appendix A.  Install Logic for SMP/E Release 6 and the Feature for Online</H1></a>
<pre width="80"><!-- * -->
 | <I>Books</I>
```
```html
<a name="A.0">A.0</a>           <a href="A.0?DT=19920330095121"><strong>Appendix A.  Install Logic for SMP/E Release 6 and the Feature for Online Books </strong></a>
```

Books and topics used as evidence: `GC23-0469-01` `A.0`; `SC09-1164-01`
(shelf `PRG1SORT`, DT `19900829171904`) `A.0`, `B.0`, `1.5.2.1`, `1.6.1.2`;
`SC26-4309-2` (shelf `IBMMMSTR`, DT `19911004151140`) `PREFACE`, `CONTENTS`,
`1.0`, `1.2`, `3.1`, `3.3`, `4.0` and the `PREFACE.*` entries; `SC26-4570-01`
(shelf `SC26-457`, DT `19911220191142`) `3.14.2.2`, `3.14.2.8`, `3.24.2.2`;
`SC09-2417-00` (DT `19961114175628`) `2.2.3.3`; `SH12-5657-04` (shelf
`SH12-565`, DT `19941206115523`) `4.7.5.3`; `SC24-5527-02` (DT
`19921218151459`) `6.4.4`; `SH20-2488-5` (shelf `ITPPIBOK`, DT
`19910628074854`) `D.1.3.4`; `GC28-1656-1` (shelf `N2AH1MST`, DT
`19910329000100`) `1.2.4`; `GC41-9678-00` (shelf `QSYSINFO`, DT
`19910524120827`) — all 45 disagreeing topics.

## Correction: hosted can arbitrate `QSYSINFO`

Earlier notes and issue #86 record that the shelf's copy of `QSYSINFO.BOO` is
a different edition and returns `COVER` for topic `2.1.21`. That is only true
when the book is requested by its **document number** `GC41-9678-00`, which
resolves to edition `GC41-9678-02` at DT `19931217171103`. Requested by its
file basename `QSYSINFO` at our own DT `19910524120827`, the shelf serves our
edition and all 45 topics:

```
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/QSYSINFO/2.1.45?DT=19910524120827
```
```html
<H3> 2.1.45   SC09-1416, Application Development Tools:  Report Layout Utility User's</H3>
```

## Conclusion

The 20 TOC-side disagreements, and `ITPPIBOK D.1.3.4`'s `<BOOK>`, are all the
same thing: the next display line's length byte, read as title text because
the flattened projection puts its dictionary spelling between one entry's
title and the next entry's `CTocE`. The normative format statement is in
`libgeist/doc/boo-spec/toc.adoc` under the `CTOCE` syntax.

The one remaining disagreement, `SC24-5527-02 6.4.4`, is unrelated: its `ST`
display line already carries the whole title and the topic title read from it
is complete, but the emitted heading stops after two words and the remainder
appears in the body. Hosted heads it with the complete title. That is a
separate defect and is still open.
