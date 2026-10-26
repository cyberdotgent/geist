// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/container/control_ir.hpp"
#include "geist/detail/lowering/document_ir.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

struct DecodedLogicalRecordSource;

// The `LNK` dialect of the CSELECT control (doc/boo-spec/markup.adoc, "LNK selector
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
//           hosted `<a href="/bookmgr/monetcoq.jpg"><img
//           src="/bookmgr/monetcoq.jpg" ...>.</a>`
//   <IMAGE> <INTERNET> </bookmgr/product.gif> <http://.../library> <> <ICON>
//           XWEBDEMO 1.0, hosted `<a href="http://.../library"><img
//           src="/bookmgr/product.gif" ...>IBM BookManager BookServer</a>`
//   <SPT>   <XDLIPRE> <> <SC33-1165> <00> <DFHA300>  GC33-1162-02 1.6.9.1
//           hosted `<a href="../../DOCNUM/SC33-1165/SPTXDLIPRE?ScrollTOP=
//           SPTXDLIPRE#SPTXDLIPRE?DocnumLevel=00">` (`GC33-116`, DT
//           19990317155853)
//
//   <FIG>   <LSA1>  <> <SH19-6817> <ANY> <LR>   SH19-6902-00 1.1.1
//           hosted `<a href="../../DOCNUM/SH19-6817/FIGLSA1?ScrollTOP=
//           FIGLSA1#FIGLSA1?DocnumLevel=ANY">` (DT 19930503083907); also
//           SC34-3058-00 2.0 `<FIG> <EJ2F00Y> <> <SC34-3056> <ANY> <EJRW2>`
//           -> `../../DOCNUM/SC34-3056/FIGEJ2F00Y?ScrollTOP=...`
//   <TBL>   <DFHE503> <> <GC33-1571> <00> <DFHE500>   dfhqaa2a CHANGES
//
// BookServer forms every kind but `<BOOK>` the same way: the document
// number, then alternative 1 and alternative 2 concatenated, so a `<TBL>`
// link names `TBL<anchor>` in the other book exactly as `<FIG>` names
// `FIG<anchor>`.  `FIG` and `TBL` are in the reader's own list of link kinds
// beside `HDR`, `BOOK` and `SPT`.
//
// So alternative 1 is the kind, alternative 2 the in-book anchor name of a
// `<HDR>` link (the reader prefixes `HDR`) or of a `<SPT>` link (prefixed
// `SPT`: the `SRSPT<id>` spot of the other book), alternative 4 the document
// number of a `<BOOK>`/`<HDR>`/`<SPT>` link or the URL of an `<OTHER>`/`<IMAGE>`
// one, and alternative 6 the target's own identifier.  An `<IMAGE>` link
// draws an image at the start of its selected columns: alternative 3 when it
// names one, else alternative 4, which is then both image and link.  Nothing
// else is displayed: the alternatives never appear on the hosted page.
enum class SelectorLinkKindIR {
  book_contents,  // another book's table of contents
  book_heading,   // a heading inside another book
  book_spot,      // a spot (`SRSPT<id>`) inside another book
  book_anchor,    // a figure or table (`SRFIG<id>`, `SRTBL<id>`) of another
                  // book
  external_link,  // a URL the reader opens
  external_image, // a link whose content starts with an image
};

struct SelectorLinkIR {
  SelectorLinkKindIR kind = SelectorLinkKindIR::book_contents;
  // The alternatives verbatim, without their angle brackets, in source
  // order.
  std::vector<std::string> alternatives;
  // Canonical destination: `DOCNUM/<docnum>/CCONTENTS`,
  // `DOCNUM/<docnum>/HDR<anchor>`, `DOCNUM/<docnum>/SPT<anchor>`,
  // `DOCNUM/<docnum>/FIG<anchor>`, `DOCNUM/<docnum>/TBL<anchor>`, or the URL
  // itself.
  std::string destination;
  // Alternative 5, the `DocnumLevel` a live BookServer appends to a
  // `DOCNUM/...` destination as a query (`?DocnumLevel=ANY`), which it uses
  // to offer a revision picker: SC24-5527-02 1.0 references SC24-5444 as
  // `<BOOK> <> <> <SC24-5444> <ANY> <HCPA3>` and hosted serves
  // `../../DOCNUM/SC24-5444/CCONTENTS?DocnumLevel=ANY` (DT 19921218151459).
  // It is not addressing inside the target book, and it is empty for the
  // references that name no level (ITPPIBOK 1.3.3).
  std::string document_level;
  // The image an `<IMAGE>` selector draws at the start of its link.
  std::string image;
};

// Parses the alternative list of a `LNK` selector from the decoded text of
// its leading payload tokens, each of which must be spelled `<...>`.
// Returns nullopt with a reason when the list is not one of the modelled
// forms.
std::optional<SelectorLinkIR> parse_selector_link(
    const std::vector<std::string>& alternative_tokens, std::string* error);

// The image an `<IMAGE>` selector draws, from its alternatives without their
// angle brackets: alternative 3, or alternative 4 when 3 is empty.  Empty
// when the tuple is not an `<IMAGE>` one or the chosen alternative is not a
// URL (a `/`-rooted path or an `http://`, `https://` or `ftp://` URL).
std::string selector_link_image(const std::vector<std::string>& alternatives);

// The destination inside the other book of a `<FIG>`/`<TBL>` link, from its
// alternatives without their angle brackets: the kind followed by
// alternative 2 (`FIGLSA1`, `TBLDFHE503`).  Empty for any other kind.
std::string cross_book_anchor(const std::vector<std::string>& alternatives);

// The record-local tokens a `LNK` selector spends on its alternative list.
//
// The alternatives follow the control's operands as single tokens each
// spelled `<...>`; they are control metadata and hosted BookServer prints no
// character of them.  A route that walks display lines has to know which
// tokens they are, or the tuple reaches the page as text -- and inside a
// fixed-width region it does so as extra rows that shift the drawn art.
//
// Empty unless `segment` really is a `cselect` whose operand target is the
// literal `LNK`.  When `alternatives` is given it receives the same tokens'
// text, in source order, ready for `parse_selector_link`.
std::vector<std::size_t> selector_link_alternative_tokens(
    const DecodedLogicalRecordSource& record, const ControlSegmentIR& segment,
    std::vector<std::string>* alternatives = nullptr);

} // namespace geist::detail
