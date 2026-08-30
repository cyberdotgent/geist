# Numbered steps are paragraphs, not ordered lists (2026-08-30)

Conclusion for issue #51, recorded so it is not re-opened: **a numbered
procedure whose ordinals are body text is rendered correctly as numbered
paragraphs.** It is not a list-reconstruction defect and must not be
"fixed" into an `<ol>`.

## What the output looks like

Two spellings occur, and both are correct:

```
1\. From the IPCS Primary Option Menu, choose option 5\.1 \(COPYDDIR\)\.
2\. Specify the source data set name or file name and the name of the dump directory.
3\. Press Enter\. IPCS displays the following output:
```

```
**1\.** Stop the AIX NetView/6000 graphical interface\.
**2\.** Issue the **ovstop** command\.
```

The second differs only because the compiler put a `CFONT` highlight over the
ordinal; the emphasis is source-proven and belongs in the output.

Counts over the checked-in `render/` export: 1,776 escaped-ordinal lines in 9
books (`ofcuseov` 121 files, `sc24-5520-00` 108, `qsysnewg` 27, `ieac6mst` 18,
`itppibok` 16, `sc24-5527-02` 8, `gg24-4302-00` 7, `sc28-1881-05` 5,
`qsysinfo` 4), and 407 bold-ordinal lines almost entirely in `sc24-5527-02`.

## Why it is correct

The numbering is **body text the compiler wrote**, not list markup. Hosted
BookServer serves these topics with **zero `<li>` and zero `<ol>`**, keeping
the numbering as text inside `<pre>` — verified directly on `SC31-711` `2.1.4`
and `SC24-5527-02` `4.7.2`. Confirmed independently by inspection of the
upstream reader and the printed documents: they show the numbering as
paragraphs too.

Several of these procedures also interleave terminal/TUI screen captures with
the steps. Promoting the steps to list items would reflow around that fixed
layout and destroy the captures, which is the same reason `SRTBL`/box/figure
regions are reproduced as verbatim art rather than reconstructed as tables.

## The error this note exists to prevent

The reasoning that led the wrong way was: *we already emit `-` bullets where
hosted emits none, so emitting `1.` items where hosted emits none is the same
case.* It is not. The bullets are drawn by the reader from list markup in the
source, so reconstructing them recovers structure the source states. The
ordinals here are characters the compiler placed in the text; there is no list
markup behind them, so promoting them would *invent* structure — precisely
what the fail-closed rule forbids.

Hosted is the oracle for words everywhere. It is not the oracle for structure
only where the source proves structure that the reader chose not to draw. That
distinction has to be established per shape, from the source, and for this
shape the source does not prove a list.

## Status

Issue #51's other half — bullet lists collapsing into prose — was a real
defect and is fixed: `SC31-711` `2.1` recovers its four-item tool list, `2.1.3`
its six-daemon list, and `2.1.4` its two-item and three-situation lists.

`tools/list_structure_diff.py` counts `escaped_ordinal_paragraphs` and
`bold_ordinal_paragraphs` so that a future change which silently converts them
into list items shows up in the differential. The counters are a regression
guard, not a backlog.
