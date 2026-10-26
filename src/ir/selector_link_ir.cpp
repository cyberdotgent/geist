// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/selector_link_ir.hpp"

#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

bool bracketed(const std::string& token) {
  return token.size() >= 2 && token.front() == '<' && token.back() == '>';
}

bool url_like(const std::string& value) {
  return (!value.empty() && value.front() == '/') ||
         ascii_starts_with_case_insensitive(value, "http://") ||
         ascii_starts_with_case_insensitive(value, "https://") ||
         ascii_starts_with_case_insensitive(value, "ftp://");
}

} // namespace

std::string cross_book_anchor(const std::vector<std::string>& alternatives) {
  if (alternatives.size() < 2) return {};
  const auto kind = ascii_lower(alternatives[0]);
  if (kind == "fig") return "FIG" + alternatives[1];
  if (kind == "tbl") return "TBL" + alternatives[1];
  // `<FIGURE>` is accepted as a figure kind and addressed by its own spelling
  // (see parse_selector_link).
  if (kind == "figure") return "FIGURE" + alternatives[1];
  return {};
}

std::string selector_link_image(const std::vector<std::string>& alternatives) {
  if (alternatives.size() < 6 || ascii_lower(alternatives[0]) != "image")
    return {};
  // Alternative 3 when the selector names one (XWEBDEMO 1.0
  // `</bookmgr/product.gif>`), else alternative 4, the link itself
  // (XWEBDEMO 1.4.1 `<> </bookmgr/monetcoq.jpg>`).
  const auto& image = alternatives[2].empty() ? alternatives[3]
                                              : alternatives[2];
  return url_like(image) ? image : std::string{};
}

std::optional<SelectorLinkIR> parse_selector_link(
    const std::vector<std::string>& alternative_tokens, std::string* error) {
  const auto reject = [&](std::string message) -> std::optional<SelectorLinkIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  // Six alternatives are the invariant; the optional seventh is the
  // BookServer base URL, which the hosted page never uses for the link.
  if (alternative_tokens.size() < 6 || alternative_tokens.size() > 7)
    return reject("LNK selector has " +
                  std::to_string(alternative_tokens.size()) +
                  " alternatives, not six or seven");
  SelectorLinkIR link;
  for (const auto& token : alternative_tokens) {
    if (!bracketed(token))
      return reject("LNK selector alternative '" + token +
                    "' is not bracketed");
    link.alternatives.push_back(token.substr(1, token.size() - 2));
  }
  const auto kind = ascii_lower(link.alternatives[0]);
  const auto& anchor = link.alternatives[1];
  const auto& fourth = link.alternatives[3];
  link.document_level = link.alternatives[4];
  if (kind == "book") {
    if (fourth.empty())
      return reject("LNK <BOOK> selector has no document number");
    link.kind = SelectorLinkKindIR::book_contents;
    link.destination = "DOCNUM/" + fourth + "/CCONTENTS";
    return link;
  }
  if (kind == "hdr") {
    if (fourth.empty() || anchor.empty())
      return reject("LNK <HDR> selector has no document number or anchor");
    link.kind = SelectorLinkKindIR::book_heading;
    link.destination = "DOCNUM/" + fourth + "/HDR" + anchor;
    return link;
  }
  if (kind == "spt") {
    if (fourth.empty() || anchor.empty())
      return reject("LNK <SPT> selector has no document number or anchor");
    link.kind = SelectorLinkKindIR::book_spot;
    link.destination = "DOCNUM/" + fourth + "/SPT" + anchor;
    return link;
  }
  // A figure or table of the other book: BookServer addresses it as the
  // kind followed by the anchor, `FIG<anchor>` or `TBL<anchor>`.
  //
  // The reader admits a kind by its first three letters only (`HDR`, `FIG`,
  // `TBL`, ...; `BOOK` by four) and then writes the kind exactly as the
  // tuple spells it in front of the anchor.  `<FIGURE>` therefore passes as
  // a figure link and is addressed `FIGURE<anchor>`: dfhqas13 1.2.3.5
  // record 87 stores `cselect 56 12 LNK <FIGURE> <DFHA205> <> <SC33-1682>
  // <00> <DFHA200>`, for which the reader's link builder writes
  // `DOCNUM/SC33-1682/FIGUREDFHA205` (no hosted page carries the kind: the
  // four books that use it are not in the hosted catalog).  Only the
  // observed spelling is admitted; any other unlisted kind still fails
  // closed.
  if (kind == "fig" || kind == "tbl" || kind == "figure") {
    if (fourth.empty() || anchor.empty())
      return reject("LNK <" + link.alternatives[0] +
                    "> selector has no document number or anchor");
    link.kind = SelectorLinkKindIR::book_anchor;
    link.destination = "DOCNUM/" + fourth + "/" + cross_book_anchor(link.alternatives);
    return link;
  }
  if (kind == "other" || kind == "internet" || kind == "image") {
    if (fourth.empty())
      return reject("LNK <" + link.alternatives[0] +
                    "> selector has no external target");
    link.kind = kind == "image" ? SelectorLinkKindIR::external_image
                                : SelectorLinkKindIR::external_link;
    link.destination = fourth;
    if (kind == "image") {
      link.image = selector_link_image(link.alternatives);
      if (link.image.empty())
        return reject("LNK <IMAGE> selector names no image URL");
    }
    return link;
  }
  return reject("LNK selector kind '" + link.alternatives[0] +
                "' is not modelled");
}

std::vector<std::size_t> selector_link_alternative_tokens(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment,
    std::vector<std::string>* alternatives) {
  if (alternatives != nullptr) alternatives->clear();
  std::vector<std::size_t> claimed;
  if (segment.kind != BookControlKind::select || segment.display_text)
    return claimed;
  // The operand list is `<column> <length> LNK`; anything else is either a
  // plain in-book selector or a spelling this dialect does not cover.
  const auto text = token_words_to_ascii(record.assembled.words);
  const auto begin = std::min<std::size_t>(segment.operand_range.begin,
                                           text.size());
  const auto end = std::min<std::size_t>(segment.operand_range.end,
                                         text.size());
  std::istringstream operands(text.substr(begin, end - begin));
  std::string column;
  std::string length;
  std::string target;
  std::string trailing;
  if (!(operands >> column >> length >> target) || (operands >> trailing))
    return claimed;
  if (ascii_lower(target) != "lnk") return claimed;
  // Everything the control spends on its opcode and operands is already
  // consumed; the alternatives are the `<...>` run directly after them.
  //
  // One alternative is not always one token.  A long URL is split across
  // several (XWEBDEMO 1.0 stores
  // `<http://booksrv2.raleigh.ibm.com/cgi-bin/bookmgr/bookmgr.cmd/li` and
  // `brary>` as two, with a zero-width spacing token between them), so a
  // group runs until the token that closes it.  The run ends at the first
  // token that opens no group -- or at a display line's length byte, which
  // the dictionary spells as an arbitrary word and which in this very record
  // spells `<OTHER>`: bracketed, and emphatically not an alternative.
  //
  // The control ranges are decoded byte ranges and the token map is in
  // words; a Latin-1 word before the control decodes to two bytes, so the
  // range must be converted or it reaches past the operands into the list
  // (SH12-2230-00-de 1.2.1 record 32 lost `<BOOK>` that way).
  const auto words = decoded_byte_range_to_word_range(
      record.assembled, {segment.opcode_range.begin, segment.operand_range.end});
  const auto owned =
      source_tokens_intersecting_output(record.assembled, words.begin,
                                        words.end);
  std::vector<std::size_t> pending_tokens;
  std::string pending;
  std::size_t groups = 0;
  for (const auto token : segment.source_tokens) {
    if (std::find(owned.begin(), owned.end(), token) != owned.end()) continue;
    if (token >= record.tokens.size()) break;
    if (is_display_line_length_token(record, token)) break;
    const auto word = trim_ascii(token_words_to_ascii(record.tokens[token]));
    if (pending.empty()) {
      if (word.empty() || word.front() != '<') break;
      pending = word;
    } else {
      pending += word;
    }
    pending_tokens.push_back(token);
    if (pending.back() != '>') continue;
    if (!bracketed(pending)) break;
    claimed.insert(claimed.end(), pending_tokens.begin(), pending_tokens.end());
    if (alternatives != nullptr) alternatives->push_back(pending);
    pending_tokens.clear();
    pending.clear();
    // Six alternatives are the invariant and a seventh is the most any
    // observed selector carries; anything past that is the row's own text.
    if (++groups == 7) break;
  }
  return claimed;
}

} // namespace geist::detail
