# BOO Decoded Markup And Control Encodings

BOO topic content decodes to a compact BookManager control stream. It is not raw
SGML, XML, or HTML. The decoded stream does preserve IBM GML and BookMaster
concepts such as heading tags, highlighted phrases, figures, tables, lists, and
cross references.

The examples below use the current experimental decoder's text rendering. That
decoder still has imperfect spacing and case recovery, so control names should
be treated as case-insensitive unless a byte-level rule says otherwise.

## Historical Lineage

External historical references confirm the lineage:

- IBM Generalized Markup Language (GML) was an IBM SCRIPT/DCF macro/tag set and
  used colon-prefixed tags such as `:h1`, `:p`, `:ol`, and `:li`.
- SGML descended from IBM GML and was standardized as ISO 8879.
- IBM SCRIPT-family documentation describes BookMaster and BookManager BUILD as
  later systems built on the SCRIPT/GML foundation, with BookManager electronic
  books compiled into `.BOO` files and served by BookServer.

References: [IBM Generalized Markup Language](https://en.wikipedia.org/wiki/IBM_Generalized_Markup_Language),
[SCRIPT markup](https://en.wikipedia.org/wiki/SCRIPT_(markup)), and
[SGML](https://en.wikipedia.org/wiki/Standard_Generalized_Markup_Language).

The BOO evidence is consistent with that history: decoded records contain GML
style names such as `:h1`, `:h2`, `:figlist`, `:tlist`, and `HP1`, but the
stored representation is a tokenized BookManager format rather than SGML text.

## Control Syntax

Decoded controls are word-like commands embedded in logical records. Some use
`KEY=value`, some use `KEY <fields>`, and some are concatenated with their
primary id.

| Form | Example | Meaning |
| --- | --- | --- |
| `KEY=value` | `CFONTDEF=1 Hp1` | Book/global property or definition. |
| `KEY <fields>` | `CTOCE 0 1 1.0 Introduction` | Structured control with positional fields. |
| `KEY<id>` | `SHcontents`, `SRFIGfig4302hp1` | Control whose first operand is concatenated to the control word. |
| `:tag` value | `CHDLEVEL :h1` | GML-derived tag name used as a BookManager topic kind or style. |

The current decoder can print separator placeholders as `?` in some records.
Those placeholders are decoder artifacts or unresolved control spacing and
should not be treated as literal document text without byte-level confirmation.

`libgeist` raw topic output projects the decoded control stream into a
GML-style script. This projection is intentionally not Markdown and not final
rendering. It is a readable, colon-tagged representation of the BookManager
controls that have been identified so far. Decoder placeholder bytes such as
`?` are not emitted as text in this projection.

## GML-Style Raw Projection

The raw projection uses one colon-prefixed tag per identified control or text
span. Attribute names are `libgeist` projection names, not verified IBM source
attribute names. The source control names remain documented here so a reader can
map the projection back to decoded BOO controls.

| Decoded control | Raw projection | Field mapping | Status |
| --- | --- | --- | --- |
| `SH<id>` | `:topic id='<id>'.` | Topic id from the concatenated suffix. | Verified topic header control. |
| `CTOPICN <n>` | `:topicn number='<n>'.` | 1-based topic number. | Verified topic header control. |
| `CPARENT <id>` | `:parent refid='<id>'.` | Parent topic id, empty when absent. | Verified topic header control. |
| `CFORWARDLEVEL <id>` | `:next refid='<id>'.` | Next topic id at the same navigation level. | Verified topic header control. |
| `CBACKLEVEL <id>` | `:prev refid='<id>'.` | Previous topic id at the same navigation level. | Verified topic header control. |
| `CSUMMARY <a> <b> <c>` | `:summary values='<a> <b> <c>'.` | Summary/count triplet. | Verified topic header control; field meanings partly open. |
| `CHDLEVEL :<tag>` | `:hlevel tag=':<tag>'.` | GML-derived heading/topic kind. | Verified topic header control. |
| `CSOURCEFN <name>` | `:source file='<name>'.` | Original source member/file name. | Observed in topic headers. |
| `ST <title/text>` | `:st.<title/text>` | Topic title or title-prefixed text in current decoder output. | Verified title control; title/body separation still partly lossy. |
| `CTOCDEF=<style> <fields>` | `:tocdef style='<style>' values='<fields>'.` | TOC style definition. | Verified in `CONTENTS` topics. |
| `CTOCE <level> <style> <id> <title>` | `:tocentry level='<level>' style='<style>' refid='<id>'.<title>` | One TOC entry. | Verified in `CONTENTS` topics. |
| `ETOC` | `:etoc.` | End of TOC entry stream. | Observed TOC terminator. |
| `CFONTDEF=<code> <name>` | `:fontdef code='<code>' style='<name>'.` | Font/style code definition. | Verified book-level style map. |
| `CFONT <triples...>` | `:font spans='<triples...>'.` | Repeated `<offset> <length> <font_code>` triples. | Verified span control; exact offset base still under study. |
| `CSELECT <col> <len> <target> [text]` | `:link col='<col>' len='<len>' refid='<target>'.<text>` | Selectable link/cross-reference. | Verified for topics, figures, tables, and pictures. |
| `CMENU` | `:menu.` | Start of menu/list of selectable items. | Observed. |
| `CMITEM <id> <text>` | `:mi refid='<id>'.<text>` | Menu item target and label. | Observed. |
| `CEMENU` | `:emenu.` | End of menu/list. | Observed. |
| `SRFIG<id>` | `:fig id='<id>'.` | Figure anchor/start id. | Observed in figure records. |
| `SREFIG` | `:efig.` | Figure end marker. | Observed, but current decoder can truncate/case-shift some occurrences. |
| `SRTBL<id>` | `:table id='<id>'.` | Table anchor/start id. | Observed. |
| `SRETBL` | `:etable.` | Table end marker. | Observed. |
| `CZ <mode> <fields>` | `:layout mode='<mode>' values='<fields>'.` | Layout/reflow control. | Observed for break, off, and flow controls. |
| `SI <fields>` | `:index.<fields>` | Search/index marker. | Observed; subfields unresolved. |
| `CITERM <fields>` | `:iterm.<fields>` | Index term marker/content. | Observed; subfields unresolved. |
| `CGPSEP <fields>` | `:indexsep.<fields>` | Index group separator. | Observed; subfields unresolved. |
| Other `C...` controls | `:control name='<control> <value>'.` | Generic preservation for recognized control-like words not yet assigned a semantic tag. | Fallback projection. |
| Plain text span | `:p.<text>` | Remaining decoded prose after known controls are separated. | Projection artifact, not a stored BOO control. |

Book-level metadata controls use the same fallback or property-specific parser
elsewhere in `libgeist`: `CLANGUAGE=`, `CVERSION=`, `CBLDVERS=`, `CREFLOW=`,
`CTITLE=`, `CSTITLE=`, `CCOPYRIGHT=`, `CSECURITY=`, `CDATE=`, `CAUTHOR=`,
and `CDOCNUM=` are documented in [logical-controls.md](logical-controls.md).
They are not normally emitted for an individual TOC topic unless they appear in
that topic's decoded raw record range.

## Topic And Heading Controls

Topic headers begin with `SH<topic_id>` and are documented in
[topics.md](topics.md). The heading/type field uses GML-like tag values:

| Control | Observed values | Role |
| --- | --- | --- |
| `SH<id>` | `SHcontents`, `SH1.0`, `SHpreface.5.1` | Topic identifier. |
| `CTOPICN` | integer | 1-based topic number. |
| `CHDLEVEL` | `:toc`, `:h1`, `:h2`, `:h3`, `:h4`, `:cover`, `:preface`, `:abstract`, `:notices`, `:vnotice`, `:figlist`, `:tlist`, `:abbrev` | Topic kind or heading depth. |
| `ST` | free text | Topic title. |
| `CPARENT`, `CFORWARDLEVEL`, `CBACKLEVEL` | topic ids | Navigation links between topics. |

Evidence:

| File | Decoded record evidence |
| --- | --- |
| `QS3X36CM.BOO` | `SHcontents ... CHDLEVEL :toc ... ST Table Of Contents` |
| `QS3X36CM.BOO` | `SH1.0 ... CHDLEVEL :h1 ... ST Introduction` |
| `GG24-4302-00.boo` | `SHfigures ... CHDLEVEL :figlist ... ST Figures` |
| `SC26-4221-08.boo` | `SHnotices ... CHDLEVEL :notices ... ST Notices` |

## Table-Of-Contents Controls

The `CONTENTS` topic stores literal TOC controls:

| Control | Syntax | Role |
| --- | --- | --- |
| `CTOCDEF` | `CTOCDEF=<style> <fields...>` | Defines TOC presentation styles. The exact numeric field meanings are not fully resolved. |
| `CTOCE` | `CTOCE <nesting> <toc_style> <topic_id> <title>` | One displayed TOC entry. |
| `ETOC` | `ETOC` | End of TOC entry list. |

Example from `QS3X36CM.BOO`:

```text
CTOCE 0 1 1.0 Introduction
```

This points to topic header:

```text
SH1.0 ... CTOPICN 4 ... CHDLEVEL :h1 ... ST Introduction
```

## Font And Highlight Controls

Book-level `CFONTDEF` controls map compact font/style codes to semantic style
names. Body-level `CFONT` controls then apply those compact codes to spans of
text in a logical record.

`QS3X36CM.BOO`, `GG24-4302-00.boo`, and `SC26-4221-08.boo` all include this
style-map pattern in the decoded book header:

| `CFONTDEF` code | Observed semantic name |
| --- | --- |
| `0` | `H0` |
| `h` through `m` | `H1` through `H6` |
| `_` | `underscore` |
| `1` | `HP1` |
| `2` | `HP2` |
| `3` | `HP3` |
| `4` through `9` | `HP4` through `HP9` |
| `a` | `apl` |
| `c` | `Cit` |
| `p` | `Pk` |
| `q` | `Pkdef` |
| `v` | `pv` |
| `z` | `pvdef` |
| `t` | `tp` |
| `r` | `rk` |
| `x` | `xph` |
| `e` | `xmp` |
| `u` | `Md` |
| `y` | `Mdqual` |

Observed body examples:

| File | Decoded record evidence | Interpretation |
| --- | --- | --- |
| `QS3X36CM.BOO` | `CFONT 7 2 1 10 9 1` | Two spans use style code `1`, which the book header defines as `HP1`. |
| `QS3X36CM.BOO` | `CFONT 12 2 x 15 6 x` | Two spans use style code `x`, defined as `xph`. |

The `CFONT` field layout observed so far is repeated triples:

```text
CFONT <column_or_offset> <span_length> <font_code> ...
```

Bold and emphasis should therefore be implemented through the `CFONT` plus
`CFONTDEF` pipeline, not by searching for literal `<b>` markup. `HP1`, `HP2`,
and `HP3` are GML-derived highlighted-phrase levels. Their exact visual mapping
to bold, italic, monospace, or other renderer styles is a presentation rule and
still needs direct renderer confirmation; the storage layer only identifies the
semantic style code.

## Cross-References And Menus

Inline links and cross references use `CSELECT` with a target id and display
span information. The target id can name a topic, figure, table, or picture.

| Control | Observed syntax | Role |
| --- | --- | --- |
| `CSELECT` | `CSELECT <column> <length> <target_id>` | Link or selectable reference. |
| `CMENU` | `CMENU` | Starts a menu/list of selectable items. |
| `CMITEM` | `CMITEM <topic_id> <text>` | Menu item target and label. |
| `CEMENU` | `CEMENU` | Ends a menu/list. |

Examples:

| File | Decoded evidence |
| --- | --- |
| `QS3X36CM.BOO` | `CMENU CMITEM 1.1 Displaying as/400 Commands Online CEMENU` |
| `GG24-4302-00.boo` | `CSELECT 3 8 fig4302hp1` |
| `SC26-4221-08.boo` | `CSELECT 7 22 hdrlanguag` |

The `column` and `length` values are display-span positions in the decoded
logical line. A reader should preserve the `target_id` even if it chooses a
different rendering layout.

## Figures, Pictures, Tables, And Asset References

Figure and table controls wrap anchored figure/table content. Picture
references are linked through `CSELECT` targets such as `pic1` or `Pic1`.

| Control | Observed syntax | Role |
| --- | --- | --- |
| `SRFIG<id>` | `SRFIGfig4302hp1` | Starts or anchors a figure. |
| `SREFIG` | `SREFIG` | Ends a figure block; current decoder can truncate or case-shift this marker in some records. |
| `SRTBL<id>` | `SRTBLv2pubs` | Starts or anchors a table. |
| `SRETBL` | `SRETBL` | Ends a table block. |
| `CSELECT ... fig...` | `CSELECT 3 8 fig4302hp1` | Selectable figure reference. |
| `CSELECT ... pic...` | `CSELECT 35 9 pic1` | Selectable picture/image placeholder. |

Evidence:

| File | Decoded evidence |
| --- | --- |
| `GG24-4302-00.boo` | `CSELECT 3 8 fig4302hp1 ... SRFIGfig4302hp1 ... CSELECT 35 9 pic1 ... Figure 1. Parallel Transaction Server` |
| `GG24-4302-00.boo` | `CSELECT 3 8 fig4302rs1 ... SRFIGfig4302rs1 ... CSELECT 35 9 pic2 ... Figure 2. Remote Site Recovery` |
| `SC26-4221-08.boo` | `CSELECT 3 10 Pic1 ... Picture 1 represents ...` |
| `SC26-4221-08.boo` | `CSELECT 3 8 Figv2pubs ... SRFIGv2pubs ... SRTBLv2pubs ... Figure 1` |

The resource table stores raw assets as documented in [assets.md](assets.md).
The exact mapping from body ids such as `pic1` to legacy resource ids such as
`1` appears to be a reader-side picture-id normalization rule and remains an
open renderer detail.

## Layout And Reflow Controls

Version 1.3 and 1.4 books contain `CZ` controls for paragraph, list, figure,
table, and box layout. These are content/layout controls rather than raw text.

Observed examples from `SC26-4221-08.boo`:

| Decoded evidence | Interpretation |
| --- | --- |
| `CZ Break 3` | Layout break before the following content. |
| `CZ off Fig` | End or disable figure layout mode. |
| `CZ off Etable` | End or disable table layout mode. |
| `CZ off Elblbox` | End or disable labeled-box layout mode. |
| `CZ flow p 3 3` | Paragraph flow control. |
| `CZ flow sl 3 3` | Simple-list flow control. |
| `CZ flow li 7 7` | List-item flow control. |
| `CZ flow dl`, `CZ flow dt`, `CZ flow dd` | Definition-list, term, and description flow controls. |

An independent reader can initially preserve these controls structurally and map
them to renderer-specific block/list constructs later.

## Index And Search Controls

Index/search-related controls appear in topic bodies and generated index
topics.

| Control | Observed role |
| --- | --- |
| `SI` | Search/index term marker in body content. |
| `CITERM` | Index term content. |
| `CGPSEP` | Index group separator. |

These controls need more fixture-driven work before their complete subfield
layout can be considered stable.

## Open Questions

- Complete byte-level separation for every inline control field and separator.
- Exact visual mapping of `HP1` through `HP9`, `xph`, `Pk`, `Cit`, and related
  styles in each IBM renderer.
- Exact normalization from picture ids in body markup (`pic1`) to raw resource
  ids in the resource descriptor table (`1`).
- Full `CZ` control grammar for all paragraph, list, table, and figure layout
  modes.
