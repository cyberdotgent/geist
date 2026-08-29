#include "geist/detail/selector_link_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
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

} // namespace geist::detail
