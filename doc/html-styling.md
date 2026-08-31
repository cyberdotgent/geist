# libgeist HTML output: classes, ids and data attributes

This is the reference for the markup libgeist's native HTML renderer emits.
The scheme is a **stable part of the API**, not an implementation detail: a
consumer must be able to append a stylesheet and style every part of the
output without patching libgeist and without scraping structure out of tag
soup.

`html-styling.css` beside this file is a working example stylesheet that
exercises every class listed here. A test asserts that the two agree in both
directions, so a class cannot be added to the renderer without being
documented and styled, and the stylesheet cannot name a class the renderer
never emits.

This document describes the library's *output*. It is deliberately not in
`doc/boo-spec/`, which is the published BOO **format** specification and
contains no API or implementation material.

## The two forms

| Entry point | Form |
| --- | --- |
| `BooDocument::html_fragment`, `BooDocument::topic_html_fragment`, `TocEntry::html_fragment` | fragment: semantic content only |
| `BooDocument::html_document`, `BooDocument::topic_html_document`, `TocEntry::html_document` | minimal complete document wrapping exactly those fragment bytes |

They are separate named entry points rather than one call with a flag. The
command-line equivalents are `boorender <book.boo> [topic-id] --html-fragment`
and `boorender <book.boo> [topic-id] --html`.

The fragment owns semantic topic markup and safe escaping. It never emits
application chrome: no site header, sidebar, book navigation, search or
branding. Those belong to the consumer.

The complete document adds only `<!doctype html>`, `<html lang>`, a `<head>`
with `<meta charset="utf-8">`, a viewport meta, a `<title>`, any configured
stylesheet links and inline stylesheet, and a `<body>` holding the fragment.

## Principles

1. **Every class is namespaced `geist-`.** The fragment is embedded in someone
   else's page; unprefixed names such as `.note` or `.figure` would collide
   with the consumer's own stylesheet.
2. **Classes name what the source proves, not how it should look.** There is
   no `geist-table-bordered`; presentation is the consumer's.
3. **One class per block, drawn from the IR type that produced it.** No
   inferred or cosmetic classes.
4. **Machine-readable fidelity.** Output that could not prove its structure is
   still emitted, and it says so on its own element, so a consumer can find
   every degraded region programmatically and trace it back.

## Structure

```html
<div class="geist-book">
  <div class="geist-topic" id="2.1.1" data-geist-topic="2.1.1"
       data-geist-severity="typed">
    <span class="geist-topic-destination" id="SPTE083I"></span>
    ... blocks ...
  </div>
  ...
</div>
```

`geist-book` appears only in whole-book output. A single-topic fragment is one
`geist-topic` element.

### Fragment root attributes

| Attribute | Meaning |
| --- | --- |
| `id` | the topic's own id, so a cross reference to the topic lands on it |
| `data-geist-topic` | the BOO topic id, unprefixed, for programmatic lookup |
| `data-geist-severity` | `typed`, `typed-degraded`, `best-effort` or `failed` |

`data-geist-severity` is the whole-topic fidelity ladder, from most to least
faithful. `typed-degraded` means at least one block inside could not prove its
structure; `best-effort` means no typed family claimed the topic and its own
source rows were emitted verbatim; `failed` means not even the source rows
could be recovered and the topic renders as a labelled diagnostic. Content is
emitted at every severity except `failed`, which carries a diagnostic naming
the topic instead of disappearing silently.

`geist-topic-destination` carries a further id the source names this same
topic by. BookManager lets one topic hold several named destinations that all
mean "this topic", and a cross reference may use any of them.

## Block classes

One per `BlockNodeIR` alternative. The list is enumerated from the variant
itself, so a new block type cannot ship without a class.

| Block IR | class | element |
| --- | --- | --- |
| `HeadingBlockIR` | `geist-heading` | `<h1>`…`<h6>` |
| `ParagraphBlockIR` | `geist-paragraph` | `<p>` |
| `AnchorBlockIR` | `geist-anchor` | `<span>` |
| `ListBlockIR` | `geist-list` | `<ul>` / `<ol>` |
| `DefinitionListBlockIR` | `geist-definition-list` | `<dl>` |
| `TableBlockIR` | `geist-table` | `<table>` |
| `PreformattedBlockIR` | `geist-preformatted` | `<pre>` |
| `NoteBlockIR` | `geist-note` | `<aside>` |
| `PublicationListBlockIR` | `geist-publication-list` | `<ul>` |
| `FigureBlockIR` | `geist-figure` | `<figure>` |
| `FootnoteBlockIR` | `geist-footnote` | `<aside>` |
| `IndexGroupBlockIR` | `geist-index-group` | `<section>` |
| `MenuBlockIR` | `geist-menu` | `<nav>` |
| `OpaqueBlockIR` | `geist-opaque` | `<pre>` |

The element is a starting point; the class does not vary with it.

### Child classes

A block's parts are addressable too.

| class | element | inside |
| --- | --- | --- |
| `geist-list-item` | `<li>` | `geist-list` |
| `geist-definition-term` | `<dt>` | `geist-definition-list` |
| `geist-definition-description` | `<dd>` | `geist-definition-list` |
| `geist-table-head` | `<thead>` | `geist-table` |
| `geist-table-body` | `<tbody>` | `geist-table` |
| `geist-table-row` | `<tr>` | `geist-table` |
| `geist-table-header-cell` | `<th>` | `geist-table-head` |
| `geist-table-cell` | `<td>` | `geist-table-body` |
| `geist-note-label` | `<p>` | `geist-note` |
| `geist-note-content` | `<p>` | `geist-note` |
| `geist-publication-entry` | `<li>` | `geist-publication-list` |
| `geist-publication-title` | `<p>` | `geist-publication-entry` |
| `geist-publication-paragraph` | `<p>` | `geist-publication-entry` |
| `geist-figure-image` | `<img>` | `geist-figure` |
| `geist-figure-caption` | `<figcaption>` | `geist-figure` |
| `geist-footnote-label` | `<span>` | `geist-footnote` |
| `geist-footnote-content` | `<span>` | `geist-footnote` |
| `geist-index-heading` | `<p>` | `geist-index-group` |
| `geist-index-entries` | `<ul>` | `geist-index-group` |
| `geist-index-entry` | `<li>` | `geist-index-entries` |
| `geist-menu-lead` | `<p>` | `geist-menu` |
| `geist-menu-items` | `<ul>` | `geist-menu` |
| `geist-menu-item` | `<li>` | `geist-menu-items` |
| `geist-menu-item-target` | `<span>` | `geist-menu-item` |
| `geist-menu-item-label` | `<span>` | `geist-menu-item` |

A `<thead>` appears only when the source proved header rows. A table the
source gave no header is emitted as body rows alone: an invented header row
would be a claim the source never made.

## Inline classes

| class | element | source |
| --- | --- | --- |
| `geist-emphasis` | `<em>` | `EmphasisInlineIR` kind `emphasis` |
| `geist-strong` | `<strong>` | `EmphasisInlineIR` kind `strong` |
| `geist-strong-emphasis` | `<strong><em>` | `EmphasisInlineIR` kind `strong_emphasis` |
| `geist-monospace` | `<code>` | `CodeInlineIR` |
| `geist-image` | `<img>` | `ImageInlineIR` |
| `geist-opaque-inline` | `<code>` | `OpaqueInlineIR` |

Semantic elements *and* the class are used, so a consumer can restyle without
fighting user-agent defaults.

There is deliberately no `geist-bold` or `geist-italic`. The book's font
styles are resolved before any renderer sees them: bold-family styles become
`strong`, italic-family styles become `emphasis`, and monospace families
become a code inline. A renderer that emitted `geist-bold` would be asserting
a presentation the typed IR no longer carries. The two names are reserved and
unused.

## Links

Base class `geist-link` plus exactly one kind modifier.

| modifier | destination |
| --- | --- |
| `geist-link--topic` | a topic of this book |
| `geist-link--anchor` | a named destination (`SR<id>`) of this book |
| `geist-link--resource` | an object stored in this book |
| `geist-link--external` | an absolute URL the source spells out |
| `geist-link--in-book` | an `SR<id>` destination named from inside a drawn row |
| `geist-link--book-contents` | another book's contents page |
| `geist-link--book-heading` | a heading inside another book |
| `geist-link--external-url` | an absolute URL named from inside a drawn row |

The first four are the typed cross-reference kinds; the last four are the
kinds a selector inside a preformatted row can name.

`geist-link--unresolved` is added when the renderer could not spell a
destination at all. Its `href` is then `#`. A cross-book reference with no
resolver configured is the normal case: BookManager books reference each other
by order number, and a single-book export cannot resolve one. The affordance
is kept, because hosted BookServer serves an anchor there too, but it is
marked so a consumer can grey it out or disable it rather than present a dead
link as live.

`geist-image--unresolved` is the same marker on an `<img>` whose resource
could not be resolved.

### Anchor roles

An `AnchorBlockIR` carries `geist-anchor` plus one role modifier saying what
kind of destination the source named:

`geist-anchor--cross-reference`, `geist-anchor--figure`,
`geist-anchor--table`, `geist-anchor--local`.

These are *destination* roles, not link kinds; a `geist-anchor--figure` is the
place a `geist-link--anchor` points at.

## IDs

Anchor ids are emitted **verbatim from the source** (`FIGFIGUNIQ1`, `MSG0123`,
`TBL…`). They are not renamed and not otherwise prefixed: intra-book cross
references target the source spelling, and a divergence between a reference id
and the emitted anchor id is invisible in the rendered page — it reads as
ordinary text — while breaking every destination it touches.

`HtmlRenderOptions::id_prefix` (empty by default) is applied to **both** every
emitted `id` and every href the renderer generates for one, through a single
function, so the two cannot diverge. Use it when a raw BOO id could collide
with ids the consumer's own page already defines.

Whole-book output can contain the same id more than once where the book itself
names one anchor id in more than one topic. That is a property of the source,
and renaming to make it unique would break the references that use it. Consumers
that need uniqueness should render per topic, or supply an `id_prefix` per
topic.

## Degraded output

Degraded output is always emitted; what these attributes add is the ability to
find it.

| Attribute | On | Meaning |
| --- | --- | --- |
| `data-geist-degraded="true"` | any block element | the block's structure could not be proven, so it was emitted verbatim |
| `data-geist-degradation="<code>"` | the same element | the stable machine-readable code naming the fallback taken, e.g. `fixed-table-verbatim` |

The topic root's `data-geist-severity` is `typed-degraded` whenever any block
inside carries these.

## Other data attributes

| Attribute | On | Meaning |
| --- | --- | --- |
| `data-geist-depth="<n>"` | `geist-list-item` | the item's nesting level inside its own list, when non-zero |
| `data-geist-empty="true"` | `geist-list-item` | the source wrote this item with no text of its own |
| `value="<n>"` | `geist-list-item` in an `<ol>` | the ordinal the source stated, which may start above one or contain gaps |
| `data-geist-generated="true"` | `geist-menu-lead` | text the reader synthesises, which no BOO byte states |
| `data-geist-opaque-kind="<kind>"` | `geist-opaque`, `geist-opaque-inline` | the source-format kind of understood-but-unrepresentable content |
| `data-geist-reason="<code>"` | `geist-diagnostic` | the reason code for a topic that could not be rendered |

## Diagnostics

A topic at severity `failed` renders as a labelled diagnostic rather than
disappearing:

```html
<aside class="geist-diagnostic" data-geist-reason="no-recoverable-source">
  <p class="geist-diagnostic-message">…</p>
  <p class="geist-diagnostic-source">…</p>
  <p class="geist-diagnostic-reason">…</p>
</aside>
```

It is deliberately visible and deliberately labelled as a Geist diagnostic
rather than as book text.

## Escaping

Escaping is a property of the renderer, not of its callers.

* All text content is escaped for `&`, `<` and `>`.
* All attribute values are escaped for `&`, `<`, `>`, `"`, `'` and the C0
  control characters.
* Only markup the renderer emits for a recognised typed record appears in the
  output. There is no route by which source bytes reach the output as markup,
  including the preformatted route.
* A preformatted row's own bytes are preserved column for column: a link
  inside a drawn row is a pair of byte offsets into the row, and nothing that
  occupies a column is inserted.
* Bytes above `0x7F` pass through unchanged; decoded book text is UTF-8, which
  is what the complete document declares.
* `HtmlDocumentOptions::inline_stylesheet` is caller-owned CSS. The renderer
  rewrites `<` in it as the CSS character escape `\3c `, which parses as the
  same character and cannot close the `<style>` element.

## Resolvers

`HtmlRenderOptions` carries the hooks that turn what the source names into a
URL in the consumer's own route scheme. Every one is optional; an unset
resolver, or one returning nothing, leaves the renderer with its own
context-free destination.

| Hook | Answers for |
| --- | --- |
| `resolve_topic` | a topic of this book, by BOO topic id |
| `resolve_anchor` | a named destination of this book, by the id the source spells — which covers figures (`FIG…`), tables (`TBL…`) and footnotes, because BookManager names all of them the same way |
| `resolve_resource` | an object stored in this book, by its object id (`resource:69` is offered as `69`) |
| `resolve_external` | an absolute URL the source spells out, so it can be rewritten or proxied |
| `resolve_cross_book` | another book, by order number, revision level and heading anchor |

`resolve_cross_book` receives the order number, the revision level a live
BookServer uses to offer a revision picker, the heading anchor of a `<HDR>`
reference, the target identifier, and the whole alternative list verbatim.
Match on the order-number stem plus the level, not on string equality: a
reference omits the edition suffix that the target book's own document number
carries, and the level is frequently empty, which means "any revision the
shelf holds" rather than a level named as such.

Without a `resolve_cross_book` the reference renders as `href="#"` with
`geist-link--unresolved`, so a document manager can supply the resolver later
without the markup having to change.
