#pragma once

// The native HTML renderer: typed Document IR in, HTML out.
//
// It is a sibling of `document_markdown_renderer.hpp` over the same input and
// shares nothing with it but the IR.  Markdown is never an intermediate here,
// and no output of this file is produced by post-processing Markdown.
//
// Escaping is a property of the renderer, not of its callers.  Every byte of
// book text passes through `escape_html_text`, every attribute value through
// `escape_html_attribute`, and the only markup in the output is markup this
// file emits for a recognised typed node.  There is no route by which source
// bytes reach the output unescaped -- including the preformatted route, which
// the Markdown renderer spells as literal `<pre>` and which is reproduced
// here with the same escaping and the same column arithmetic.

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/html.hpp"
#include "geist/render_diagnostic.hpp"

#include <string>
#include <vector>

namespace geist::detail {

// Text content: `&`, `<` and `>`.  Bytes above 0x7F are UTF-8 continuation
// bytes of decoded book text and pass through untouched.
std::string escape_html_text(const std::string &value);

// Attribute values, always used with double quotes: text escaping plus `"`,
// `'` and the C0 control characters.
std::string escape_html_attribute(const std::string &value);

// The id the fragment emits for a source id: the source's own spelling with
// the configured prefix in front of it, and nothing else.  Every emitted
// `id=` and every href the renderer generates for one goes through this, so
// a reference and its destination cannot be spelled differently.
std::string html_emitted_id(const std::string &id,
                            const geist::HtmlRenderOptions &options);

// The class the styling contract gives each `BlockNodeIR` alternative, e.g.
// "geist-paragraph".  Exposed so a test can assert that the documented class
// list and the IR cannot drift apart.
std::string html_block_class(const BlockNodeIR &node);

// Every block class the renderer can emit, in `BlockNodeIR` order.
std::vector<std::string> html_block_classes();

// The blocks of a document, with no root element around them.
std::string render_document_html_blocks(const DocumentIR &document,
                                        const geist::HtmlRenderOptions &options);

// True when any block of the document was emitted verbatim because its
// structure could not be proven, which is what makes the topic
// `typed-degraded` rather than `typed`.
bool document_has_degraded_block(const DocumentIR &document);

// `<div class="geist-topic" ...>` ... `</div>` around already-rendered inner
// HTML.  The root is where the topic identity and the fidelity of the whole
// topic are stated; see the styling reference.
//
// The root also *is* the topic's destination: it carries the topic id as its
// `id`, and one empty `geist-topic-destination` span for every further id the
// source names this same topic by.  BookManager lets one topic carry several
// named destinations that all mean "this topic" (`SRMSG AMD083I` and
// `SRSPTE083I` side by side), and a cross reference may use any of them, so
// each has to exist in the output for the reference to land.
std::string
render_html_topic_root(const std::string &topic_id,
                       const std::vector<std::string> &named_destinations,
                       geist::RenderSeverity severity,
                       const geist::HtmlRenderOptions &options,
                       const std::string &inner_html);

// A whole document as a fragment: its blocks inside their topic root.
std::string
render_document_html_fragment(const DocumentIR &document,
                              const geist::HtmlRenderOptions &options = {});

// Drawn rows as the inside of a `<pre class="geist-preformatted">`: each
// row's own bytes, escaped, with an `<a>` opened and closed at the byte
// offsets each selector names.  Nothing that occupies a column is inserted,
// so the rendered row is the drawn row, column for column.  Shared by the
// preformatted block of a typed document and by the verbatim topic route, so
// the two cannot diverge.
std::string render_verbatim_rows_html(const std::vector<VerbatimRowIR> &rows,
                                      const geist::HtmlRenderOptions &options);

// A minimal complete document around an already-rendered fragment.  The
// fragment bytes are copied unchanged, which is what makes the two forms
// answer the same question.
std::string render_html_document(const std::string &fragment,
                                 const geist::HtmlDocumentOptions &options);

} // namespace geist::detail
