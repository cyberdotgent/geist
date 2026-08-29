#pragma once

#include "geist/detail/document_ir.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

// The `LNK` dialect of the CSELECT control (Format/markup.md, "LNK selector
// alternatives").  A selector whose operand target is the literal `LNK`
// carries its real destination in the leading tokens of its payload, each a
// single decoded token spelled `<...>`:
//
//   cselect <col> <len> LNK <kind> <a2> <a3> <a4> <a5> <a6> [<server>]
//
// Six alternatives are always present; XWEBDEMO 1.4.2 adds a seventh, the
// BookServer base URL.  Observed kinds, each verified against the hosted
// BookServer page of the topic that carries it:
//
//   <BOOK>  <>       <> <SC31-6008> <>    <TPNSDF>   ITPPIBOK 1.3.3
//           hosted `<a href="../../DOCNUM/SC31-6008/CCONTENTS">`
//   <HDR>   <FMTRTVI> <> <SC41-4801> <>   <4801>     SC41-485 1.2.3
//           hosted `<a href="../../DOCNUM/SC41-4801/HDRFMTRTVI#HDRFMTRTVI">`
//   <OTHER> <INTERNET> <> <http://www.ibm.com/> <> <IBMHOME>  XWEBDEMO 1.4.4
//           hosted `<a href="http://www.ibm.com/">`
//   <IMAGE> <INTERNET> <> </bookmgr/monetcoq.jpg> <> <MONET1> XWEBDEMO 1.4.1
//           hosted `<img src="/bookmgr/monetcoq.jpg">`
//
// So alternative 1 is the kind, alternative 2 the in-book anchor name of a
// `<HDR>` link (the reader prefixes `HDR`), alternative 4 the document
// number of a `<BOOK>`/`<HDR>` link or the URL of an `<OTHER>`/`<IMAGE>`
// one, and alternative 6 the target's own identifier.  Nothing else is
// displayed: the alternatives never appear on the hosted page.
enum class SelectorLinkKindIR {
  book_contents,  // another book's table of contents
  book_heading,   // a heading inside another book
  external_link,  // a URL the reader opens
  external_image, // a URL the reader shows inline as an image
};

struct SelectorLinkIR {
  SelectorLinkKindIR kind = SelectorLinkKindIR::book_contents;
  // The alternatives verbatim, without their angle brackets, in source
  // order.
  std::vector<std::string> alternatives;
  // Canonical destination: `DOCNUM/<docnum>/CCONTENTS`,
  // `DOCNUM/<docnum>/HDR<anchor>`, or the URL itself.
  std::string destination;
};

// Parses the alternative list of a `LNK` selector from the decoded text of
// its leading payload tokens, each of which must be spelled `<...>`.
// Returns nullopt with a reason when the list is not one of the modelled
// forms.
std::optional<SelectorLinkIR> parse_selector_link(
    const std::vector<std::string>& alternative_tokens, std::string* error);

} // namespace geist::detail
