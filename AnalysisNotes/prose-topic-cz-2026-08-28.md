# CZ-dialect prose topics: hosted sources and workflow (2026-08-28)

Supporting notes for the `CZ` slice of the ordinary prose topic family
(issue #58). The normative format facts are in
[`Format/markup.md`](../Format/markup.md), section "CZ layout directives".

## Hosted book ids for the CZ fixtures

Three of the four CZ books are served under a **truncated** id, not under
their local BOO basename, which is why
`BookServerCache/SC09-2417-00/` and `BookServerCache/GX27-3999-00/` contain
BookServer *message* pages rather than topic bodies. The catalog was searched
with

```text
http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/FINDBOOK?filter=&SUBMIT=Find
```

whose rows print `<short title>  <hosted id>  <timestamp>  <document number>`.
The timestamp equals the local `booinfo` timestamp, which confirms each
mapping.

| Local BOO | Title | Hosted id | `DT` |
| --- | --- | --- | --- |
| `SC09-2417-00.boo` | VisualAge for C++ for AS/400 | `SC09-241` | `19961114175628` |
| `GX27-3999-00.boo` | Dual EtherStreamer MC 32 Adapter | `GX27-399` | `19950730184057` |
| `SC41-485.boo` | OS/400 Configuration APIs | `SC41-485` | `19951003131222` |
| `packet.boo` | Amateur Packet Radio | `packet` | `20260614112503` |

Fetch a topic with

```text
curl -s 'http://cbrdoc01.lan.cyber.gent/bookmgr/bookmgr.exe/BOOKS/<id>/<topic>?DT=<dt>'
```

The pages are ISO-8859-1; search them with Python rather than `grep`.

## Comparison procedure used for this slice

For every sampled topic: fetch the hosted page, strip the banner and trailer
(the text between the first and last `<hr>`), remove inline formatting tags
(`<B>`, `<I>`, `<kbd>`, `<samp>`, `<a>`, ...) **without** inserting a space so
`<kbd>name</kbd>,` stays one word, replace block tags with a space, unescape
entities, and tokenise. Render the same topic with `boorender <book> <topic>
--md` from the current build and from a build of the merge base, unescape
Markdown, drop link syntax, and tokenise the same way. Compare with
`difflib.SequenceMatcher`; a typed score below the legacy score is a
regression that has to be explained.

Two renderer conventions are normalised on both sides before scoring, because
they are cross-family decisions of the typed Markdown renderer and not
properties of this slice:

- a definition/note label is written `**label:** ...` while hosted `<dt>`
  carries no separator, so a trailing `:` is dropped from every token;
- `*` and `` ` `` are emphasis/code delimiters in the Markdown and are
  removed (this also hides a literal `*` inside a code span, so code examples
  were additionally diffed by eye).

## Difference classes seen in the 48-topic sample

| Class | Decision |
| --- | --- |
| `- **term:** definition` vs hosted `<dt>term<dd>definition` | Renderer convention, shared with the glossary and publication families. Accepted. |
| `&` written `&amp;` (`SC41-485` 1.2.5 `&amp;1`) | `escape_markdown_text` convention of the typed renderer; renders back to `&1`. Accepted. |
| ` ``` ` fence with no info string where the legacy route wrote ` ```text ` | Typed renderer convention. Accepted; `packet_markdown_test` updated. |
| Ordered-list numbers present in the Markdown (`GX27-3999-00` 2.1, 2.8) | Markdown carries the ordinal that hosted renders from `<ol>`. Equal to the legacy route. Accepted. |
| `packet` 3.2 keeps `an`, 6.3.1.2 `(`/`an`, 4.4.3 `which`, 3.7.2 `are` | Compact one-byte dictionary words before a fill/origin pair that hosted drops. Recorded as an open residual in `Format/markup.md`; the two candidate rules tried (row-width fit, run length equal to the directive indent) each broke verified topics elsewhere, so the model keeps them as text and fails no topic over it. |
| `SC09-2417-00` 3.1.4.2 drops four `\` characters | Continuation backslashes of a C macro example. Open; the row shape is not yet distinguished from a row-control slot. |

Everything else in the sample is word-identical to hosted.
