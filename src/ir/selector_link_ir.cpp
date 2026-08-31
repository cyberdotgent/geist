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

} // namespace

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
  if (kind == "other" || kind == "internet" || kind == "image") {
    if (fourth.empty())
      return reject("LNK <" + link.alternatives[0] +
                    "> selector has no external target");
    link.kind = kind == "image" ? SelectorLinkKindIR::external_image
                                : SelectorLinkKindIR::external_link;
    link.destination = fourth;
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
  const auto owned = source_tokens_intersecting_output(
      record.assembled, segment.opcode_range.begin, segment.operand_range.end);
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
