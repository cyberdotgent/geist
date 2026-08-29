# Trailing menu label vs canonical catalog title (issue #58)

Investigated while adding the generated CONTENTS/INDEX family. Not fixed in
that slice: the fix belongs in topic-title extraction, whose blast radius
(every heading, every `boo2git` filename, the whole TOC) is larger than the
slice could re-verify. The diagnosis below is complete and the fix is
positional, not a comparison relaxation.

## The class

45 topics still fail closed on

```
prose topic rejected: trailing menu targets rejected:
  raw menu label differs from canonical catalog title: <target>
```

(43 on that message, 2 on the `... beyond its compact terminal token` variant).
`validate_source_menu_targets` in `libgeist/src/menu_topic_ir.cpp` compares the
raw `CMITEM` label with `BookTopicCatalogEntryIR::topic_header->title`, which
comes from `TopicInfo` and therefore from the flattened decoded string.

Enumerated with a probe over all 45 (`probe3`: raw menu items, the catalog
entry's header title and every TOC projection, then the validation verdict):

| Shape | Topics |
| --- | ---: |
| label is a whole-word prefix of the header title; target has no TOC projection | 20 |
| header title additionally retains the literal `ST` opcode; target has no TOC projection | 20 |
| label equals a TOC projection which is a strict prefix of the header title | 5 |

All 45 are one defect with three surface forms: **the catalog's header title is
the target topic's whole `ST` payload run, continued past the row break into
the topic's body**, and in 20 cases the `ST` opcode word itself is left in
front of it. 40 of the 45 targets are below TOC depth and carry no TOC
projection at all, which is why the earlier `build_menu` TOC fallback
(`AnalysisNotes/prose-selector-menu-2026-08-29.md`, bucket B) could not reach
them.

## Byte-level evidence

`GC28-183.boo`, menu topic `5.8.1`, item target `5.8.1.1`, label
`Multiple Destinations`. The catalog header title is

```
ST  Multiple Destinations???????????????? SI Destinations, Multiple
    for Example, to Print a Report in Chicago, New York, Paris, and
    Los Angeles
```

The target's own records are 728..729, and record 728's display lines are

```
line 7: csourcefn IEAB5EST
line 8: ST  Multiple Destinations
line 9:
line 10: SI destinations, multiple
line 11:    For example, to print a report in Chicago, New York, Paris, and Los
```

Line 8 is exactly the label. Lines 9..11 are separate display lines that the
string-level title extraction ran together, and the `ST` opcode of line 8 was
kept as title text.

`IBMMMSTR.boo` `2.0` -> `2.1` is the TOC-projection form of the same defect:
header title `Diagnostic Messagesand    for Every Linkage Editor Job ...`
(note the glued `Messagesand`, a row break with no space), TOC projection
`Diagnostic Messages`, menu label `Diagnostic Messages`.

`GG24-395.boo` `2.3.3.1` -> `2.3.3.1.1` is the no-opcode form: header
`Common Transport Semantics??????????????????????????????? SI Open Blueprint`,
label `Common Transport Semantics`.

## The fix

A topic header's title is the visible text of its `ST` **display line**, which
`record_display_lines` / `display_line_text`
(`libgeist/src/geist/detail/display_lines.hpp`) already compute exactly, and
whose opcode word is the line's first space-delimited word. Deriving the
catalog's header title that way makes all three surface forms disappear at
once and needs no comparison relaxation in `validate_source_menu_targets`.

The cost is the blast radius: `TopicInfo::title` is also the heading text, the
`boo2git` filename source and the TOC label, so the change needs a whole-corpus
differential of its own with a hosted check on every topic whose title moves.
Do not instead relax the label comparison to "label is a prefix of the header
title": that would admit a label that is a genuine prefix of a genuinely
different title, and it would hide the extraction defect rather than fix it.
