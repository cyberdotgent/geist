// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Native HTML output (issue #46).
//
// HTML is a sibling of the Markdown renderer over the same typed Document IR,
// not a conversion of rendered Markdown: Markdown is not a lossless
// intermediate representation and is never the canonical one.
//
// Two forms are offered, and they are separate named entry points rather than
// one call with a flag:
//
//   * a *fragment* -- semantic topic content only, for a consumer that owns
//     the surrounding page, its navigation and its stylesheet; and
//   * a *complete document* -- a minimal standalone page that wraps exactly
//     the same fragment bytes.
//
// The class, id and data-attribute scheme the fragment emits is a documented
// part of this API, so a consumer can append a stylesheet and style every
// part of the output without patching libgeist. See
// `libgeist/doc/html-styling.md` and the example stylesheet beside it.

#include "geist/export.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace geist {

// What a rendered anchor points at, as the source states it.  The first four
// are the typed cross-reference kinds; the last four are the kinds a
// selector inside a drawn (preformatted) row can name.
enum class HtmlLinkKind {
  // A topic of this book.
  topic,
  // A named destination (`SR<id>`), which includes the anchors a figure, a
  // table and a footnote are reached by.
  anchor,
  // An object stored in this book, e.g. `resource:69`.
  resource,
  // An absolute URL the source spells out.
  external,
  // An `SR<id>` destination named from inside a drawn row.
  in_book,
  // Another book's contents page.
  book_contents,
  // A heading inside another book.
  book_heading,
  // A spot (`SRSPT<id>`) inside another book.
  book_spot,
  // An absolute URL named from inside a drawn row.
  external_url,
};

GEIST_API const char* html_link_kind_name(HtmlLinkKind kind) noexcept;

// A reference to a *different* book, as BookManager states it: an order
// number, the revision level a live BookServer uses to offer a revision
// picker, and -- for a `<HDR>` reference -- the heading anchor inside the
// target book.
//
// A single-book export cannot resolve one of these, so libgeist does not
// invent a destination for it.  A consumer holding a shelf of books answers
// it here; see `HtmlRenderOptions::resolve_cross_book`.
struct HtmlCrossBookReference {
  // Alternative 4: the order number of the referenced book, e.g. `SC24-5518`.
  // Match on the order-number stem plus the level, not on string equality:
  // the reference omits the edition suffix a book's own document number
  // carries (`SC31-6055` names the book whose document number is
  // `SC31-6055-1`).
  std::string document_number;
  // Alternative 5: the revision level a live BookServer uses to offer a
  // revision picker.  Often empty, and sometimes `ANY`; a resolver must treat
  // an empty level as "any revision the shelf holds" rather than as a level.
  std::string document_level;
  // Alternative 2: the heading anchor of a `<HDR>` reference.
  std::string heading_anchor;
  // Alternative 2: the spot anchor of a `<SPT>` reference.  Hosted BookServer
  // addresses it in the other book as `SPT<anchor>`, where a `<HDR>` anchor
  // is `HDR<anchor>`.  Empty for every other kind.
  std::string spot_anchor;
  // Alternative 6: the target identifier the selector names.
  std::string target;
  // The whole alternative list verbatim, in source order, without its angle
  // brackets, so a consumer can use a field libgeist does not name here.
  std::vector<std::string> alternatives;
};

// The size, in CSS pixels, an `<img>` is to be displayed at.  See
// `ResourceEntry::display_width` and `HtmlRenderOptions::resolve_image_size`.
struct HtmlImageSize {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

// How the renderer turns what the source names into a URL the consumer's
// route scheme can serve.
//
// Every resolver is optional.  A resolver that is unset, or that returns
// `std::nullopt`, leaves the renderer with its own context-free destination:
// the value the source states for a topic, resource or URL, and `#<id>` for
// an in-book anchor.  A cross-book reference has no context-free destination
// at all, so it renders as `href="#"` carrying the `geist-link--unresolved`
// class -- the affordance is kept and marked, never presented as a live link.
struct HtmlRenderOptions {
  // A topic of this book, by its BOO topic id.
  std::function<std::optional<std::string>(const std::string& topic_id)>
      resolve_topic;
  // A named destination inside this book, by the id the source spells.  This
  // is the hook for figures (`FIG<id>`), tables (`TBL<id>`) and footnotes
  // (`SRFTN<id>`) as well as plain cross-reference anchors: BookManager
  // names all four the same way, and the renderer does not guess a role the
  // reference site does not state.
  std::function<std::optional<std::string>(const std::string& anchor_id)>
      resolve_anchor;
  // An object stored in this book, e.g. the `69` of `resource:69`, or the
  // whole reference when it is not a stored object.  The latter is an image
  // URL the book spells, such as the `/bookmgr/...` path of an external
  // figure or of the image that leads an image link; check the book's
  // resources before treating a value as one of its objects.
  std::function<std::optional<std::string>(const std::string& resource)>
      resolve_resource;
  // The display size of the image an `<img>` shows, asked with the same value
  // `resolve_resource` receives.  An answer becomes the element's `width` and
  // `height` attributes; `std::nullopt` (or a zero dimension) emits neither.
  //
  // Left unset, a topic rendered through `BooDocument` or its `TocEntry`
  // answers from the book's own resources (`ResourceEntry::display_width`),
  // which is what scales a full-resolution legacy fax bitmap back to the
  // size hosted BookServer presents it at.  Set it to override that, or to a
  // function returning `std::nullopt` to leave every image at its natural
  // size.
  std::function<std::optional<HtmlImageSize>(const std::string& resource)>
      resolve_image_size;
  // An absolute URL the source spells out.  Supplied so a consumer can
  // rewrite or proxy it; returning `std::nullopt` keeps the source URL.
  //
  // A kept source URL is published only when its scheme is one that
  // navigates -- `http`, `https`, `ftp`, `ftps` or `mailto` -- or when it
  // names no scheme at all and is therefore relative.  Anything else, a
  // `javascript:` or `data:` URL above all, is kept as a dead link rather
  // than published as one a click would follow: a book is data, and the
  // renderer will not assert that a destination it found inside one is safe
  // to hand a browser.  The same rule guards a topic id and a resource
  // reference used verbatim.  This resolver is consulted first and its answer
  // is used as given, so a consumer that has its own reason to publish such a
  // URL can still do so.
  std::function<std::optional<std::string>(const std::string& url)>
      resolve_external;
  // Another book.  See `HtmlCrossBookReference`.
  std::function<std::optional<std::string>(const HtmlCrossBookReference&)>
      resolve_cross_book;
  // Applied to every id the fragment emits *and* to every href the renderer
  // generates for one, so the two can never diverge.  Empty by default: a
  // BOO id is emitted verbatim unless a consumer asks otherwise, because
  // intra-book cross references already target the verbatim spelling.
  std::string id_prefix;
};

// The page a complete HTML document puts around the fragment.
struct HtmlDocumentOptions {
  // `<title>`.  Empty means the renderer uses the book or topic title it was
  // given; if that is empty too, no `<title>` text is invented.
  std::string title;
  // `<html lang>`.  BOO states no document language, so this is the
  // consumer's declaration, not the book's.
  std::string language = "en";
  // Emitted as `<link rel="stylesheet" href="...">`, in order.
  std::vector<std::string> stylesheets;
  // Emitted inside a single `<style>` element.  The caller owns the CSS; the
  // renderer only makes it impossible for it to leave that element, by
  // rewriting every `<` as the CSS character escape `\3c ` (which parses as
  // the same character and cannot close the element).
  std::string inline_stylesheet;
};

} // namespace geist
