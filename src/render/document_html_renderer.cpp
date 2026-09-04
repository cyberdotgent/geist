// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Native HTML rendering of the typed Document IR (issue #46).
//
// A sibling of the Markdown renderer, not a post-processor of it.  See
// geist/detail/document_html_renderer.hpp for the contract, and
// libgeist/doc/html-styling.md for the class/id/data-attribute scheme this
// file is the sole producer of.
#include "geist/detail/render/document_html_renderer.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace geist::detail {
namespace {

// One resolved destination.  `unresolved` says the renderer could not spell
// a destination at all -- not that it used its own fallback -- and it is the
// only thing that puts `geist-link--unresolved` on an anchor.
struct HrefIR {
  std::string href;
  bool unresolved = false;
};

HrefIR dead_link() { return HrefIR{"#", true}; }

// Whether a URL a book spells may be published as a live href.
//
// A book is data, and one comes from wherever its reader found it.  A
// `javascript:` or `data:` URL spelled inside it would run in the origin of
// whatever serves the book the moment a reader clicks the link, so the
// renderer will not vouch for one: it keeps the affordance and marks it dead,
// exactly as it does for a destination it cannot spell.  This is the
// renderer declining to assert something it cannot know, not a presentation
// decision -- a consumer that does want such a URL still gets the first say
// through `resolve_external`, whose answer is used verbatim.
//
// The scheme is read the way a browser reads it.  ASCII whitespace and C0
// controls are removed first because a browser strips TAB, LF and CR out of a
// URL before parsing it, which is what makes `java&#9;script:` navigate for a
// reader while reading as an unknown scheme to a naive prefix test.
bool safe_href_scheme(const std::string &url) {
  std::string cleaned;
  cleaned.reserve(url.size());
  for (const char raw_ch : url) {
    const auto byte = static_cast<unsigned char>(raw_ch);
    if (byte > 0x20 && byte != 0x7f)
      cleaned.push_back(raw_ch);
  }
  const auto colon = cleaned.find(':');
  // No scheme at all: a relative reference, which cannot name a scheme and
  // so cannot execute.  A `/`, `?` or `#` ahead of the colon puts the colon
  // inside a path, query or fragment rather than in a scheme delimiter.
  if (colon == std::string::npos || cleaned.find_first_of("/?#") < colon)
    return true;
  std::string scheme = cleaned.substr(0, colon);
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](const unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return scheme == "http" || scheme == "https" || scheme == "ftp" ||
         scheme == "ftps" || scheme == "mailto";
}

std::string prefixed_id(const std::string &id,
                        const geist::HtmlRenderOptions &options) {
  return html_emitted_id(id, options);
}

// The one place an in-book anchor becomes an href.  It calls the same
// `prefixed_id` the emitted `id=` attribute uses, so a reference and the
// destination it names cannot be spelled differently -- a divergence that
// once broke 243 cross-file destinations while still reading as ordinary
// text.
HrefIR anchor_href(const std::string &id,
                   const geist::HtmlRenderOptions &options) {
  if (id.empty())
    return dead_link();
  if (options.resolve_anchor) {
    if (const auto resolved = options.resolve_anchor(id))
      return resolved->empty() ? dead_link() : HrefIR{*resolved, false};
  }
  return HrefIR{"#" + prefixed_id(id, options), false};
}

HrefIR topic_href(const std::string &id,
                  const geist::HtmlRenderOptions &options) {
  if (options.resolve_topic) {
    if (const auto resolved = options.resolve_topic(id))
      return resolved->empty() ? dead_link() : HrefIR{*resolved, false};
  }
  if (id.empty() || !safe_href_scheme(id))
    return dead_link();
  return HrefIR{id, false};
}

// `resource:69` names object 69 of this book; anything else is passed whole,
// because only the `resource:` spelling proves a stored object.
std::string resource_object_id(const std::string &resource) {
  static constexpr std::string_view marker = "resource:";
  if (resource.size() > marker.size() &&
      resource.compare(0, marker.size(), marker) == 0)
    return resource.substr(marker.size());
  return resource;
}

HrefIR resource_href(const std::string &resource,
                     const geist::HtmlRenderOptions &options) {
  if (options.resolve_resource) {
    if (const auto resolved = options.resolve_resource(
            resource_object_id(resource)))
      return resolved->empty() ? dead_link() : HrefIR{*resolved, false};
  }
  // Deliberately not scheme-checked. The value here is the book's own
  // `resource:<id>` spelling, which no browser resolves and which nothing
  // executes; blanking it would drop the object id a consumer without a
  // `resolve_resource` still reads out of the emitted markup.
  return resource.empty() ? dead_link() : HrefIR{resource, false};
}

HrefIR external_href(const std::string &url,
                     const geist::HtmlRenderOptions &options) {
  if (options.resolve_external) {
    if (const auto resolved = options.resolve_external(url))
      return resolved->empty() ? dead_link() : HrefIR{*resolved, false};
  }
  if (url.empty() || !safe_href_scheme(url))
    return dead_link();
  return HrefIR{url, false};
}

geist::HtmlLinkKind link_kind(CrossReferenceTargetKindIR kind) {
  switch (kind) {
  case CrossReferenceTargetKindIR::topic:
    return geist::HtmlLinkKind::topic;
  case CrossReferenceTargetKindIR::anchor:
    return geist::HtmlLinkKind::anchor;
  case CrossReferenceTargetKindIR::resource:
    return geist::HtmlLinkKind::resource;
  case CrossReferenceTargetKindIR::external:
    return geist::HtmlLinkKind::external;
  }
  throw std::logic_error("invalid cross-reference target kind");
}

HrefIR cross_reference_href(const CrossReferenceTargetIR &target,
                            const geist::HtmlRenderOptions &options) {
  switch (target.kind) {
  case CrossReferenceTargetKindIR::topic:
    return topic_href(target.value, options);
  case CrossReferenceTargetKindIR::anchor:
    return anchor_href(target.value, options);
  case CrossReferenceTargetKindIR::resource:
    return resource_href(target.value, options);
  case CrossReferenceTargetKindIR::external:
    return external_href(target.value, options);
  }
  throw std::logic_error("invalid cross-reference target kind");
}

geist::HtmlLinkKind verbatim_link_kind(VerbatimLinkKindIR kind) {
  switch (kind) {
  case VerbatimLinkKindIR::in_book:
    return geist::HtmlLinkKind::in_book;
  case VerbatimLinkKindIR::book_contents:
    return geist::HtmlLinkKind::book_contents;
  case VerbatimLinkKindIR::book_heading:
    return geist::HtmlLinkKind::book_heading;
  case VerbatimLinkKindIR::external_url:
    return geist::HtmlLinkKind::external_url;
  }
  throw std::logic_error("invalid verbatim link kind");
}

// A cross-book reference has no destination a single book can prove, so the
// consumer's resolver is the only thing that can spell one.  Without it the
// anchor is kept -- hosted BookServer serves one, and the node carries every
// field of the selector -- and marked dead, so a consumer can grey it out
// rather than present it as live.
HrefIR verbatim_link_href(const VerbatimLinkIR &link,
                          const geist::HtmlRenderOptions &options) {
  switch (link.kind) {
  case VerbatimLinkKindIR::in_book:
    return anchor_href(link.target, options);
  case VerbatimLinkKindIR::external_url:
    return external_href(link.url, options);
  case VerbatimLinkKindIR::book_contents:
  case VerbatimLinkKindIR::book_heading:
    break;
  }
  if (options.resolve_cross_book) {
    geist::HtmlCrossBookReference reference;
    reference.document_number = link.document_number;
    reference.document_level = link.document_level;
    reference.heading_anchor = link.heading_anchor;
    reference.target = link.target;
    reference.alternatives = link.alternatives;
    if (const auto resolved = options.resolve_cross_book(reference))
      if (!resolved->empty())
        return HrefIR{*resolved, false};
  }
  return dead_link();
}

std::string link_class(geist::HtmlLinkKind kind, bool unresolved) {
  std::string result = "geist-link geist-link--";
  result += geist::html_link_kind_name(kind);
  if (unresolved)
    result += " geist-link--unresolved";
  return result;
}

const char *anchor_role_name(AnchorRoleIR role) {
  switch (role) {
  case AnchorRoleIR::cross_reference:
    return "cross-reference";
  case AnchorRoleIR::figure:
    return "figure";
  case AnchorRoleIR::table:
    return "table";
  case AnchorRoleIR::local:
    return "local";
  }
  throw std::logic_error("invalid anchor role");
}

// Hosted BookServer names a figure's image by the picture it shows, never by
// the figure caption; the Markdown renderer carries the same note.
std::string figure_alt_text(const std::string &resource) {
  static constexpr std::string_view marker = "resource:";
  if (resource.size() > marker.size() &&
      resource.compare(0, marker.size(), marker) == 0)
    return "PICTURE " + resource.substr(marker.size());
  return resource;
}

class HtmlSink {
public:
  explicit HtmlSink(const geist::HtmlRenderOptions &options)
      : options_(options) {}

  const geist::HtmlRenderOptions &options() const noexcept { return options_; }
  std::string release() { return std::move(out_); }

  // Markup this renderer emits.  Never a source byte.
  void markup(std::string_view value) { out_.append(value); }
  // Book text, always escaped on the way in.
  void text(const std::string &value) { out_ += escape_html_text(value); }
  void attribute(std::string_view name, const std::string &value) {
    out_ += ' ';
    out_.append(name);
    out_ += "=\"";
    out_ += escape_html_attribute(value);
    out_ += '"';
  }
  void newline() { out_ += '\n'; }

private:
  std::string out_;
  const geist::HtmlRenderOptions &options_;
};

void append_anchor(HtmlSink &sink, const HrefIR &href,
                   geist::HtmlLinkKind kind, const std::string &label) {
  sink.markup("<a");
  sink.attribute("class", link_class(kind, href.unresolved));
  sink.attribute("href", href.href);
  sink.markup(">");
  sink.text(label);
  sink.markup("</a>");
}

void append_inlines(HtmlSink &sink, const InlineSequenceIR &inlines);

void append_inline(HtmlSink &sink, const InlineIR &node) {
  std::visit(
      [&](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, TextInlineIR>) {
          sink.text(value.text);
        } else if constexpr (std::is_same_v<T, EmphasisInlineIR>) {
          switch (value.kind) {
          case EmphasisKindIR::emphasis:
            sink.markup("<em class=\"geist-emphasis\">");
            sink.text(value.text);
            sink.markup("</em>");
            break;
          case EmphasisKindIR::strong:
            sink.markup("<strong class=\"geist-strong\">");
            sink.text(value.text);
            sink.markup("</strong>");
            break;
          case EmphasisKindIR::strong_emphasis:
            sink.markup("<strong class=\"geist-strong-emphasis\"><em>");
            sink.text(value.text);
            sink.markup("</em></strong>");
            break;
          }
        } else if constexpr (std::is_same_v<T, CodeInlineIR>) {
          sink.markup("<code class=\"geist-monospace\">");
          sink.text(value.code);
          sink.markup("</code>");
        } else if constexpr (std::is_same_v<T, CrossReferenceInlineIR>) {
          const auto href =
              cross_reference_href(value.target, sink.options());
          append_anchor(sink, href, link_kind(value.target.kind),
                        value.label.empty() ? value.target.value
                                            : value.label);
        } else if constexpr (std::is_same_v<T, ImageInlineIR>) {
          const auto href = resource_href(value.resource, sink.options());
          sink.markup("<img");
          sink.attribute("class", href.unresolved
                                      ? "geist-image geist-image--unresolved"
                                      : "geist-image");
          sink.attribute("src", href.href);
          sink.attribute("alt", value.alt_text);
          sink.markup(">");
        } else if constexpr (std::is_same_v<T, HardBreakInlineIR>) {
          sink.markup("<br>");
        } else if constexpr (std::is_same_v<T, OpaqueInlineIR>) {
          sink.markup("<code class=\"geist-opaque-inline\"");
          sink.attribute("data-geist-opaque-kind", value.kind);
          sink.markup(">");
          sink.text(value.content);
          sink.markup("</code>");
        }
      },
      node.node);
}

void append_inlines(HtmlSink &sink, const InlineSequenceIR &inlines) {
  for (const auto &node : inlines)
    append_inline(sink, node);
}

// The `#81` requirement made machine-readable: a block the pipeline could not
// prove the structure of says so on its own element, with the stable code
// that names the fallback it took, so a consumer can find every degraded
// region programmatically and trace it back to its source records.
void append_degradation_attributes(HtmlSink &sink,
                                   const DocumentNodeOriginIR &origin) {
  if (origin.fidelity != DocumentFidelityIR::degraded)
    return;
  sink.attribute("data-geist-degraded", "true");
  sink.attribute("data-geist-degradation", origin.degradation_code.empty()
                                               ? std::string("unnamed")
                                               : origin.degradation_code);
}

// Opens a block element carrying its contract class and, when the block is
// degraded, its degradation attributes.
void open_block(HtmlSink &sink, std::string_view tag,
                const std::string &block_class,
                const DocumentNodeOriginIR &origin) {
  sink.markup("<");
  sink.markup(tag);
  sink.attribute("class", block_class);
  append_degradation_attributes(sink, origin);
  sink.markup(">");
}

void close_block(HtmlSink &sink, std::string_view tag) {
  sink.markup("</");
  sink.markup(tag);
  sink.markup(">");
}

void append_verbatim_row(HtmlSink &sink, const VerbatimRowIR &row) {
  std::size_t cursor = 0;
  for (const auto &link : row.links) {
    if (link.begin < cursor || link.end > row.text.size() ||
        link.begin >= link.end)
      continue;
    sink.text(row.text.substr(cursor, link.begin - cursor));
    append_anchor(sink, verbatim_link_href(link, sink.options()),
                  verbatim_link_kind(link.kind),
                  row.text.substr(link.begin, link.end - link.begin));
    cursor = link.end;
  }
  sink.text(row.text.substr(cursor));
}

// A preformatted block is emitted as `<pre>` whether or not it carries
// links: the rows are the source's own columns either way, and the anchors a
// drawn row names sit at the byte offsets inside it.
void append_preformatted(HtmlSink &sink, const PreformattedBlockIR &node,
                         const DocumentNodeOriginIR &origin,
                         const std::string &block_class) {
  open_block(sink, "pre", block_class, origin);
  for (std::size_t index = 0; index < node.lines.size(); ++index) {
    if (index != 0)
      sink.newline();
    VerbatimRowIR row;
    row.text = node.lines[index];
    if (index < node.line_links.size())
      row.links = node.line_links[index];
    append_verbatim_row(sink, row);
  }
  close_block(sink, "pre");
}

void append_table(HtmlSink &sink, const TableBlockIR &node,
                  const DocumentNodeOriginIR &origin) {
  open_block(sink, "table", "geist-table", origin);
  // A headerless source table gets no `<thead>`: an invented header row is a
  // claim about structure the source did not make.
  if (node.header_rows != 0) {
    sink.markup("<thead class=\"geist-table-head\">");
    for (std::size_t index = 0; index < node.header_rows; ++index) {
      sink.markup("<tr class=\"geist-table-row\">");
      for (const auto &cell : node.rows[index].cells) {
        sink.markup("<th class=\"geist-table-header-cell\" scope=\"col\">");
        append_inlines(sink, cell.content);
        sink.markup("</th>");
      }
      sink.markup("</tr>");
    }
    sink.markup("</thead>");
  }
  if (node.header_rows < node.rows.size()) {
    sink.markup("<tbody class=\"geist-table-body\">");
    for (auto index = node.header_rows; index < node.rows.size(); ++index) {
      sink.markup("<tr class=\"geist-table-row\">");
      for (const auto &cell : node.rows[index].cells) {
        sink.markup("<td class=\"geist-table-cell\">");
        append_inlines(sink, cell.content);
        sink.markup("</td>");
      }
      sink.markup("</tr>");
    }
    sink.markup("</tbody>");
  }
  close_block(sink, "table");
}

void append_block(HtmlSink &sink, const BlockIR &block) {
  const auto &origin = block.origin;
  const auto block_class = html_block_class(block.node);
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, HeadingBlockIR>) {
          const auto level =
              std::min<std::uint32_t>(std::max<std::uint32_t>(node.level, 1), 6);
          const auto tag = "h" + std::to_string(level);
          open_block(sink, tag, block_class, origin);
          append_inlines(sink, node.content);
          close_block(sink, tag);
        } else if constexpr (std::is_same_v<T, ParagraphBlockIR>) {
          open_block(sink, "p", block_class, origin);
          append_inlines(sink, node.content);
          close_block(sink, "p");
        } else if constexpr (std::is_same_v<T, AnchorBlockIR>) {
          // The id is the source's own spelling, carried through unrenamed;
          // only the configured prefix is added, and the same prefix is added
          // to every href that names it.
          sink.markup("<span");
          sink.attribute("class", block_class + " geist-anchor--" +
                                      anchor_role_name(node.role));
          sink.attribute("id", prefixed_id(node.id, sink.options()));
          append_degradation_attributes(sink, origin);
          sink.markup("></span>");
        } else if constexpr (std::is_same_v<T, ListBlockIR>) {
          const auto *tag = node.ordered ? "ol" : "ul";
          open_block(sink, tag, block_class, origin);
          for (const auto &item : node.items) {
            sink.markup("<li class=\"geist-list-item\"");
            if (item.depth != 0)
              sink.attribute("data-geist-depth", std::to_string(item.depth));
            if (node.ordered && item.source_ordinal)
              sink.attribute("value", std::to_string(*item.source_ordinal));
            if (item.empty_content)
              sink.attribute("data-geist-empty", "true");
            sink.markup(">");
            append_inlines(sink, item.content);
            sink.markup("</li>");
          }
          close_block(sink, tag);
        } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
          open_block(sink, "dl", block_class, origin);
          for (const auto &entry : node.entries) {
            sink.markup("<dt class=\"geist-definition-term\">");
            append_inlines(sink, entry.term);
            sink.markup("</dt><dd class=\"geist-definition-description\">");
            append_inlines(sink, entry.definition);
            sink.markup("</dd>");
          }
          close_block(sink, "dl");
        } else if constexpr (std::is_same_v<T, TableBlockIR>) {
          append_table(sink, node, origin);
        } else if constexpr (std::is_same_v<T, PreformattedBlockIR>) {
          append_preformatted(sink, node, origin, block_class);
        } else if constexpr (std::is_same_v<T, NoteBlockIR>) {
          open_block(sink, "aside", block_class, origin);
          if (!node.label.empty()) {
            sink.markup("<p class=\"geist-note-label\">");
            append_inlines(sink, node.label);
            sink.markup("</p>");
          }
          sink.markup("<p class=\"geist-note-content\">");
          append_inlines(sink, node.content);
          sink.markup("</p>");
          close_block(sink, "aside");
        } else if constexpr (std::is_same_v<T, PublicationListBlockIR>) {
          open_block(sink, "ul", block_class, origin);
          for (const auto &entry : node.entries) {
            sink.markup("<li class=\"geist-publication-entry\">");
            sink.markup("<p class=\"geist-publication-title\">");
            append_inlines(sink, entry.title);
            sink.markup("</p>");
            for (const auto &paragraph : entry.paragraphs) {
              sink.markup("<p class=\"geist-publication-paragraph\">");
              append_inlines(sink, paragraph);
              sink.markup("</p>");
            }
            sink.markup("</li>");
          }
          close_block(sink, "ul");
        } else if constexpr (std::is_same_v<T, FigureBlockIR>) {
          const auto href = resource_href(node.resource, sink.options());
          open_block(sink, "figure", block_class, origin);
          sink.markup("<img");
          sink.attribute("class",
                         href.unresolved
                             ? "geist-figure-image geist-image--unresolved"
                             : "geist-figure-image");
          sink.attribute("src", href.href);
          // The book's own description of the picture when it gave one
          // (BookMaster `:artdesc`), else the picture's name as hosted
          // BookServer spells it.
          sink.attribute("alt", node.description.empty()
                                    ? figure_alt_text(node.resource)
                                    : node.description);
          sink.markup(">");
          if (!node.caption.empty()) {
            sink.markup("<figcaption class=\"geist-figure-caption\">");
            append_inlines(sink, node.caption);
            sink.markup("</figcaption>");
          }
          close_block(sink, "figure");
        } else if constexpr (std::is_same_v<T, FootnoteBlockIR>) {
          sink.markup("<aside");
          sink.attribute("class", block_class);
          sink.attribute("id", prefixed_id(node.id, sink.options()));
          append_degradation_attributes(sink, origin);
          sink.markup("><span class=\"geist-footnote-label\">");
          sink.text(node.id);
          sink.markup("</span><span class=\"geist-footnote-content\">");
          append_inlines(sink, node.content);
          sink.markup("</span></aside>");
        } else if constexpr (std::is_same_v<T, IndexGroupBlockIR>) {
          open_block(sink, "section", block_class, origin);
          if (!node.heading.empty()) {
            sink.markup("<p class=\"geist-index-heading\">");
            append_inlines(sink, node.heading);
            sink.markup("</p>");
          }
          sink.markup("<ul class=\"geist-index-entries\">");
          for (const auto &entry : node.entries) {
            sink.markup("<li class=\"geist-index-entry\">");
            // The lowering already formed the destination: a leading `#`
            // names an anchor of this book, anything else names a topic.
            const auto anchor = !entry.target.empty() &&
                                entry.target.front() == '#';
            const auto href =
                anchor ? anchor_href(entry.target.substr(1), sink.options())
                       : topic_href(entry.target, sink.options());
            sink.markup("<a");
            sink.attribute("class",
                           link_class(anchor ? geist::HtmlLinkKind::anchor
                                             : geist::HtmlLinkKind::topic,
                                      href.unresolved));
            sink.attribute("href", href.href);
            sink.markup(">");
            append_inlines(sink, entry.term);
            sink.markup("</a></li>");
          }
          sink.markup("</ul>");
          close_block(sink, "section");
        } else if constexpr (std::is_same_v<T, MenuBlockIR>) {
          open_block(sink, "nav", block_class, origin);
          // The `Subtopics:` lead and the `<topic id> ` label prefix are
          // reader presentation, not source text; they are marked as
          // renderer-generated so a consumer can suppress them.
          sink.markup("<p class=\"geist-menu-lead\" "
                      "data-geist-generated=\"true\">Subtopics:</p>");
          sink.markup("<ul class=\"geist-menu-items\">");
          for (const auto &item : node.items) {
            sink.markup("<li class=\"geist-menu-item\">");
            const auto href =
                item.target.kind == CrossReferenceTargetKindIR::topic
                    ? topic_href(item.target.value, sink.options())
                    : cross_reference_href(item.target, sink.options());
            sink.markup("<a");
            sink.attribute("class",
                           link_class(link_kind(item.target.kind),
                                      href.unresolved));
            sink.attribute("href", href.href);
            sink.markup("><span class=\"geist-menu-item-target\">");
            sink.text(item.target.value);
            sink.markup("</span> <span class=\"geist-menu-item-label\">");
            sink.text(item.label);
            sink.markup("</span></a>");
            sink.markup("</li>");
          }
          sink.markup("</ul>");
          close_block(sink, "nav");
        } else if constexpr (std::is_same_v<T, OpaqueBlockIR>) {
          sink.markup("<pre");
          sink.attribute("class", block_class);
          sink.attribute("data-geist-opaque-kind", node.kind);
          append_degradation_attributes(sink, origin);
          sink.markup(">");
          sink.text(node.content);
          sink.markup("</pre>");
        } else {
          throw std::logic_error(
              "legacy region reached the typed HTML renderer");
        }
      },
      block.node);
}

// CSS has no comments-in-strings problem here; the only thing that must not
// survive is a sequence that could close the `<style>` element.  `\3c ` is
// the CSS escape for `<` and parses as that character.
std::string css_safe(const std::string &value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    if (ch == '<')
      output += "\\3c ";
    else
      output.push_back(ch);
  }
  return output;
}

template <std::size_t... Indexes>
std::vector<std::string> block_classes_for(std::index_sequence<Indexes...>) {
  return {html_block_class(BlockNodeIR(std::in_place_index<Indexes>))...};
}

} // namespace

std::string html_emitted_id(const std::string &id,
                            const geist::HtmlRenderOptions &options) {
  return options.id_prefix + id;
}

std::string escape_html_text(const std::string &value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    switch (ch) {
    case '&':
      output += "&amp;";
      break;
    case '<':
      output += "&lt;";
      break;
    case '>':
      output += "&gt;";
      break;
    default:
      output.push_back(ch);
    }
  }
  return output;
}

std::string escape_html_attribute(const std::string &value) {
  // Built into a string rather than an ostringstream, as `escape_html_text`
  // beside it is: this runs on every attribute value the renderer emits, and
  // a stream costs a locale-aware formatted write per character to produce
  // the same bytes.
  std::string output;
  output.reserve(value.size());
  for (const auto raw_ch : value) {
    const auto ch = static_cast<unsigned char>(raw_ch);
    switch (raw_ch) {
    case '&':
      output += "&amp;";
      break;
    case '<':
      output += "&lt;";
      break;
    case '>':
      output += "&gt;";
      break;
    case '"':
      output += "&quot;";
      break;
    case '\'':
      output += "&#39;";
      break;
    default:
      // C0 controls and DEL cannot appear literally in an attribute value.
      // Bytes above 0x7F are UTF-8 continuation bytes of decoded book text
      // and are passed through as they are.
      if (ch < 0x20 || ch == 0x7f) {
        static constexpr char kHex[] = "0123456789ABCDEF";
        output += "&#x";
        if (ch >= 0x10)
          output += kHex[ch >> 4];
        output += kHex[ch & 0x0f];
        output += ';';
      } else {
        output.push_back(raw_ch);
      }
    }
  }
  return output;
}

std::string html_block_class(const BlockNodeIR &node) {
  return std::visit(
      [](const auto &block) -> std::string {
        using T = std::decay_t<decltype(block)>;
        if constexpr (std::is_same_v<T, HeadingBlockIR>)
          return "geist-heading";
        else if constexpr (std::is_same_v<T, ParagraphBlockIR>)
          return "geist-paragraph";
        else if constexpr (std::is_same_v<T, AnchorBlockIR>)
          return "geist-anchor";
        else if constexpr (std::is_same_v<T, ListBlockIR>)
          return "geist-list";
        else if constexpr (std::is_same_v<T, DefinitionListBlockIR>)
          return "geist-definition-list";
        else if constexpr (std::is_same_v<T, TableBlockIR>)
          return "geist-table";
        else if constexpr (std::is_same_v<T, PreformattedBlockIR>)
          return "geist-preformatted";
        else if constexpr (std::is_same_v<T, NoteBlockIR>)
          return "geist-note";
        else if constexpr (std::is_same_v<T, PublicationListBlockIR>)
          return "geist-publication-list";
        else if constexpr (std::is_same_v<T, FigureBlockIR>)
          return "geist-figure";
        else if constexpr (std::is_same_v<T, FootnoteBlockIR>)
          return "geist-footnote";
        else if constexpr (std::is_same_v<T, IndexGroupBlockIR>)
          return "geist-index-group";
        else if constexpr (std::is_same_v<T, MenuBlockIR>)
          return "geist-menu";
        else if constexpr (std::is_same_v<T, OpaqueBlockIR>)
          return "geist-opaque";
        else
          // A block IR alternative with no styling class is a compile error,
          // not a runtime one: the documented class list is part of the API,
          // so a new block type cannot ship without an entry in it.
          static_assert(sizeof(T) == 0,
                        "every BlockNodeIR alternative needs a geist- class");
      },
      node);
}

std::vector<std::string> html_block_classes() {
  // One entry per `BlockNodeIR` alternative, enumerated from the variant
  // itself rather than from a hand-written list beside it.
  return block_classes_for(
      std::make_index_sequence<std::variant_size_v<BlockNodeIR>>{});
}

bool document_has_degraded_block(const DocumentIR &document) {
  for (const auto &block : document.blocks)
    if (block.origin.fidelity == DocumentFidelityIR::degraded)
      return true;
  return false;
}

std::string
render_document_html_blocks(const DocumentIR &document,
                            const geist::HtmlRenderOptions &options) {
  HtmlSink sink(options);
  for (std::size_t index = 0; index < document.blocks.size(); ++index) {
    if (index != 0)
      sink.newline();
    append_block(sink, document.blocks[index]);
  }
  return sink.release();
}

std::string
render_html_topic_root(const std::string &topic_id,
                       const std::vector<std::string> &named_destinations,
                       geist::RenderSeverity severity,
                       const geist::HtmlRenderOptions &options,
                       const std::string &inner_html) {
  HtmlSink sink(options);
  sink.markup("<div");
  sink.attribute("class", "geist-topic");
  if (!topic_id.empty()) {
    sink.attribute("id", html_emitted_id(topic_id, options));
    sink.attribute("data-geist-topic", topic_id);
  }
  // The same spelling the public severity ladder uses, so the attribute and
  // `geist::to_string(RenderSeverity)` cannot drift apart.
  sink.attribute("data-geist-severity", geist::to_string(severity));
  sink.markup(">\n");
  for (const auto &destination : named_destinations) {
    if (destination.empty() || destination == topic_id)
      continue;
    sink.markup("<span");
    sink.attribute("class", "geist-topic-destination");
    sink.attribute("id", html_emitted_id(destination, options));
    sink.markup("></span>\n");
  }
  auto out = sink.release();
  out += inner_html;
  if (!out.empty() && out.back() != '\n')
    out += '\n';
  out += "</div>\n";
  return out;
}

std::string
render_document_html_fragment(const DocumentIR &document,
                              const geist::HtmlRenderOptions &options) {
  std::string error;
  if (!verify_document_ir(document, &error))
    throw std::invalid_argument("invalid DocumentIR: " + error);
  const auto severity = document_has_degraded_block(document)
                            ? geist::RenderSeverity::typed_degraded
                            : geist::RenderSeverity::typed;
  return render_html_topic_root(document.topic.id, document.named_destinations,
                                severity, options,
                                render_document_html_blocks(document, options));
}

std::string render_verbatim_rows_html(const std::vector<VerbatimRowIR> &rows,
                                      const geist::HtmlRenderOptions &options) {
  HtmlSink sink(options);
  sink.markup("<pre class=\"geist-preformatted\">");
  for (std::size_t index = 0; index < rows.size(); ++index) {
    if (index != 0)
      sink.newline();
    append_verbatim_row(sink, rows[index]);
  }
  sink.markup("</pre>");
  return sink.release();
}

std::string render_html_document(const std::string &fragment,
                                 const geist::HtmlDocumentOptions &options) {
  std::string out = "<!doctype html>\n<html";
  if (!options.language.empty())
    out += " lang=\"" + escape_html_attribute(options.language) + "\"";
  out += ">\n<head>\n<meta charset=\"utf-8\">\n";
  out += "<meta name=\"viewport\" content=\"width=device-width, "
         "initial-scale=1\">\n";
  out += "<title>" + escape_html_text(options.title) + "</title>\n";
  for (const auto &stylesheet : options.stylesheets)
    out += "<link rel=\"stylesheet\" href=\"" +
           escape_html_attribute(stylesheet) + "\">\n";
  if (!options.inline_stylesheet.empty())
    out += "<style>\n" + css_safe(options.inline_stylesheet) + "\n</style>\n";
  out += "</head>\n<body>\n";
  out += fragment;
  if (!out.empty() && out.back() != '\n')
    out += '\n';
  out += "</body>\n</html>\n";
  return out;
}

} // namespace geist::detail

namespace geist {

const char *html_link_kind_name(HtmlLinkKind kind) noexcept {
  switch (kind) {
  case HtmlLinkKind::topic:
    return "topic";
  case HtmlLinkKind::anchor:
    return "anchor";
  case HtmlLinkKind::resource:
    return "resource";
  case HtmlLinkKind::external:
    return "external";
  case HtmlLinkKind::in_book:
    return "in-book";
  case HtmlLinkKind::book_contents:
    return "book-contents";
  case HtmlLinkKind::book_heading:
    return "book-heading";
  case HtmlLinkKind::external_url:
    return "external-url";
  }
  return "unknown";
}

} // namespace geist
