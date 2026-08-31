// Cross references a verbatim topic carries (issue #72).
//
// `cselect <column> <length> <target>` opens its own display line and marks
// that column range of the row it precedes.  A topic no typed family claims
// still names those references, and hosted BookServer serves them as `<a
// href>` *inside* the row of its `<pre>` -- so the verbatim rendering has to
// place the anchor on those columns without moving a single byte of the row.
//
// The `LNK` dialect names another book, and its alternative list is control
// metadata: hosted prints no character of it.  Left in, the tuple reaches the
// page as a row of its own and pushes drawn box art apart, which is worse
// than the missing link.
//
// Everything here is synthetic: the tests build `DecodedLogicalRecordSource`
// values by hand and open no book (issue #59).

#include "geist/detail/render_diagnostic_ir.hpp"
#include "geist/detail/verbatim_cross_references.hpp"
#include "test_failures.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;
using geist::detail::VerbatimCrossReferenceIR;
using geist::detail::VerbatimLinkKindIR;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    geist_test::record_failure();
  }
}

void append(DecodedLogicalRecordSource &record, std::uint16_t encoded,
            std::uint8_t width, TokenWords words) {
  record.encoded_tokens.push_back({encoded, width});
  record.tokens.push_back(std::move(words));
}

// A display line: its length byte, then the words of the line.  The byte's
// value is the width the line's tokens occupy, which is what the record
// decoder walks.
void line(DecodedLogicalRecordSource &record,
          const std::vector<std::string> &words) {
  std::uint16_t width = 0;
  for (std::size_t index = 0; index < words.size(); ++index)
    width = static_cast<std::uint16_t>(width + 2);
  append(record, width, 1, {3, 'r', 'o', 'w'});
  for (const auto &word : words) {
    TokenWords token;
    for (const auto ch : word)
      token.push_back(
          static_cast<std::uint16_t>(static_cast<unsigned char>(ch)));
    append(record, 0x40, 2, std::move(token));
  }
}

// Rebuilds the typed IR of a hand-assembled record and lets the decoder decide
// the framing, exactly as `decode_record_payload_ir` does in production.
void refresh(DecodedLogicalRecordSource &record) {
  record.assembled =
      geist::detail::assemble_logical_record_with_sources(record.tokens);
  record.ir.logical_record = record.logical_record;
  record.ir.tokens.clear();
  std::uint32_t byte = 0;
  for (std::size_t token = 0; token < record.tokens.size(); ++token) {
    const auto encoded = record.encoded_tokens[token];
    const auto spacing = !record.tokens[token].empty() &&
                         record.tokens[token].front() < 4;
    record.ir.tokens.push_back(
        {token, encoded, record.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)}, spacing,
         spacing ? record.tokens[token].front() : std::uint16_t{3}});
    byte += encoded.width;
  }
  record.ir.payload_range = {0, byte};
  geist::detail::assign_display_line_framing(record.ir);
  record.control_segments = geist::detail::decode_control_segments(
      record.logical_record, record.assembled, record.encoded_tokens,
      record.ir.display_lines);
}

VerbatimCrossReferenceIR linked(
    const std::vector<DecodedLogicalRecordSource> &sources,
    const std::vector<std::string> &printed_footnotes = {}) {
  return geist::detail::link_verbatim_cross_references(
      sources, geist::detail::best_effort_display_lines(sources, {}),
      printed_footnotes);
}

std::vector<std::string> row_text(const VerbatimCrossReferenceIR &linked) {
  std::vector<std::string> rows;
  for (const auto &row : linked.rows) rows.push_back(row.text);
  return rows;
}

// The row `Alpha Beta Gamma` preceded by one `cselect`: hosted wraps exactly
// the columns the operands name, which for `6 4` is the word `Beta`.
DecodedLogicalRecordSource marked_row(const std::string &operands) {
  DecodedLogicalRecordSource record;
  record.logical_record = 1;
  std::vector<std::string> control{"cselect"};
  std::size_t begin = 0;
  while (begin < operands.size()) {
    auto end = operands.find(' ', begin);
    if (end == std::string::npos) end = operands.size();
    control.push_back(operands.substr(begin, end - begin));
    begin = end + 1;
  }
  line(record, control);
  line(record, {"Alpha", "Beta", "Gamma"});
  refresh(record);
  return record;
}

// The selector's own display line draws nothing, so the row it marks is the
// next row the rendering emits.  The link covers exactly the marked columns,
// and the row's own bytes are untouched: a link is a pair of offsets, never a
// byte inserted into the row.
void a_selector_marks_the_row_it_precedes() {
  const std::vector<DecodedLogicalRecordSource> sources{
      marked_row("6 4 HDRTGT")};
  const auto plain = geist::detail::best_effort_lines(sources, {});
  const auto result = linked(sources);
  require(plain.size() == 1 && result.rows.size() == 1,
          "the synthetic topic did not emit exactly one verbatim row");
  if (plain.size() != 1 || result.rows.size() != 1) return;
  require(plain[0] == "Alpha Beta Gamma",
          "the synthetic row reads '" + plain[0] + "'");
  const auto &row = result.rows[0];
  require(row.text == plain[0],
          "the verbatim row changed: '" + row.text + "'");
  require(row.links.size() == 1, "the marked columns were not linked");
  if (row.links.size() != 1) return;
  require(row.links[0].begin == 6 && row.links[0].end == 10,
          "the link does not cover the marked columns");
  require(row.links[0].kind == VerbatimLinkKindIR::in_book &&
              row.links[0].target == "HDRTGT",
          "the link does not name the selector's target");
  require(geist::detail::render_verbatim_row(row) ==
              "Alpha <a href=\"#HDRTGT\">Beta</a> Gamma",
          "the rendered row is '" + geist::detail::render_verbatim_row(row) +
              "'");
}

// A column range naming more columns than the row drew is clamped to the
// row's end: a row ends where it ends, and that is what hosted serves
// (SC09-138 8.1.10.4 marks 60 columns of a 61-column row and links the whole
// phrase).  A range that *starts* past the row names no text and is declined.
void a_span_past_the_row_is_clamped_not_declined() {
  const auto clamped = linked({marked_row("6 40 HDRTGT")});
  require(clamped.rows.size() == 1 && clamped.rows[0].links.size() == 1,
          "a span reaching past the row's end was declined");
  if (clamped.rows.size() == 1 && clamped.rows[0].links.size() == 1)
    require(geist::detail::render_verbatim_row(clamped.rows[0]) ==
                "Alpha <a href=\"#HDRTGT\">Beta Gamma</a>",
            "the clamped span is '" +
                geist::detail::render_verbatim_row(clamped.rows[0]) + "'");

  const auto outside = linked({marked_row("40 4 HDRTGT")});
  require(outside.rows.size() == 1 && outside.rows[0].links.empty(),
          "a span starting past the row was linked anyway");
}

// A picture selector places a stored object.  The verbatim route draws no
// picture, and must not publish a text link to one.
void a_picture_selector_is_declined() {
  const auto result = linked({marked_row("6 4 PIC29")});
  require(result.rows.size() == 1 && result.rows[0].links.empty(),
          "a picture selector was linked as a cross reference");
}

// A footnote reference may leave the page that prints it, so it is linked
// either way -- but only the topic that really prints the footnote reports
// the local anchor, because emitting a second one elsewhere would invent a
// destination the source does not carry.
//
// Hosted BookServer settles this: SC31-6055-1 `BIBLIOGRAPHY.1` (DT
// 19911015203151) carries seven `cselect <col> 4 FTNMERBIB` references and
// answers every one with `BIBLIOGRAPHY?DT=19911015203151#FTNMERBIB`, the
// footnote printed by the parent topic.  Where no topic of the book prints
// the footnote at all, the export unlinks the reference -- that is the
// fail-closed step, and it is the whole book's question, not this topic's.
void a_footnote_target_may_live_on_another_page() {
  const std::vector<DecodedLogicalRecordSource> sources{
      marked_row("6 4 FTNUNIQ1")};
  const auto absent = linked(sources);
  require(absent.rows.size() == 1 && absent.rows[0].links.size() == 1,
          "a footnote printed by another topic was not linked");
  require(absent.footnote_anchors.empty(),
          "a topic that does not print the footnote reported a local anchor "
          "for it");

  const auto present = linked(sources, {"FTNUNIQ1"});
  require(present.rows.size() == 1 && present.rows[0].links.size() == 1,
          "a footnote this topic prints was not linked");
  require(present.footnote_anchors.size() == 1 &&
              present.footnote_anchors[0] == "FTNUNIQ1",
          "the footnote anchor the link needs was not reported");
}

// A topic with no selector at all is untouched.
void a_topic_without_selectors_is_untouched() {
  DecodedLogicalRecordSource record;
  record.logical_record = 1;
  line(record, {"Alpha", "Beta", "Gamma"});
  refresh(record);
  const std::vector<DecodedLogicalRecordSource> sources{record};
  const auto result = linked(sources);
  require(row_text(result) == geist::detail::best_effort_lines(sources, {}) &&
              result.rows.size() == 1 && result.rows[0].links.empty(),
          "a topic carrying no selector was rewritten");
}

DecodedLogicalRecordSource lnk_row(
    const std::vector<std::string> &alternatives) {
  DecodedLogicalRecordSource record;
  record.logical_record = 1;
  std::vector<std::string> control{"cselect", "6", "4", "LNK"};
  control.insert(control.end(), alternatives.begin(), alternatives.end());
  line(record, control);
  line(record, {"Alpha", "Beta", "Gamma"});
  refresh(record);
  return record;
}

// The `LNK` alternative list never reaches the page.  This is the second half
// of the defect: before the fix the tuple was drawn as its own row, so a
// verbatim topic lost the link *and* gained a row that pushed its box art
// apart.
void a_lnk_alternative_list_never_reaches_the_row() {
  const std::vector<DecodedLogicalRecordSource> sources{
      lnk_row({"<BOOK>", "<>", "<>", "<SC24-5444>", "<ANY>", "<HCPA3>"})};
  const auto plain = geist::detail::best_effort_lines(sources, {});
  require(plain.size() == 1 && plain[0] == "Alpha Beta Gamma",
          "the LNK alternative list was drawn as verbatim text");
  for (const auto &row : plain)
    require(row.find("<BOOK>") == std::string::npos &&
                row.find("<SC24-5444>") == std::string::npos,
            "the raw selector tuple survived into the row: '" + row + "'");
}

// A cross-book reference is a link hosted serves, and this node carries every
// field of it so a backend with a resolver (#46) can address it.  Markdown
// cannot: the book referenced is not in this export, so the anchor leads
// nowhere rather than dangling on an invented destination.
void a_cross_book_reference_carries_every_field() {
  const auto result = linked(
      {lnk_row({"<BOOK>", "<>", "<>", "<SC24-5444>", "<ANY>", "<HCPA3>"})});
  require(result.rows.size() == 1 && result.rows[0].links.size() == 1,
          "the cross-book reference was not linked");
  if (result.rows.empty() || result.rows[0].links.size() != 1) return;
  const auto &link = result.rows[0].links[0];
  require(link.kind == VerbatimLinkKindIR::book_contents,
          "the cross-book reference lost its kind");
  require(link.alternatives.size() == 6 &&
              link.alternatives[0] == "BOOK" && link.alternatives[1].empty() &&
              link.alternatives[2].empty() &&
              link.alternatives[3] == "SC24-5444" &&
              link.alternatives[4] == "ANY" && link.alternatives[5] == "HCPA3",
          "the node did not carry all six selector fields");
  require(link.document_number == "SC24-5444" && link.document_level == "ANY" &&
              link.target == "HCPA3",
          "the node did not name the order number, level and target");
  require(link.begin == 6 && link.end == 10,
          "the cross-book link does not cover the marked columns");
  require(geist::detail::render_verbatim_row(result.rows[0]) ==
              "Alpha <a href=\"#\">Beta</a> Gamma",
          "a cross-book reference must lead nowhere in Markdown, not dangle");
}

// An external reference is the one cross-book form a single-book export can
// prove: the URL is the destination, so it is spelled.
void an_external_reference_resolves_to_its_url() {
  const auto result = linked({lnk_row({"<OTHER>", "<INTERNET>", "<>",
                                       "<http://www.ibm.com/>", "<>",
                                       "<IBMHOME>"})});
  require(result.rows.size() == 1 && result.rows[0].links.size() == 1,
          "the external reference was not linked");
  if (result.rows.empty() || result.rows[0].links.size() != 1) return;
  require(result.rows[0].links[0].kind == VerbatimLinkKindIR::external_url &&
              result.rows[0].links[0].url == "http://www.ibm.com/",
          "the external reference lost its URL");
  require(geist::detail::render_verbatim_row(result.rows[0]) ==
              "Alpha <a href=\"http://www.ibm.com/\">Beta</a> Gamma",
          "the external reference was not spelled as its own URL");
}

// A `LNK` list that does not parse names nothing this route can prove, so the
// row stays plain -- and the alternatives it did spell still never draw.
void an_unparsable_lnk_list_is_declined() {
  const std::vector<DecodedLogicalRecordSource> sources{
      lnk_row({"<NOSUCHKIND>", "<>", "<>", "<X>", "<>", "<Y>"})};
  const auto result = linked(sources);
  require(result.rows.size() == 1 && result.rows[0].links.empty(),
          "an unmodelled LNK kind was linked");
  require(result.rows.size() == 1 && result.rows[0].text == "Alpha Beta Gamma",
          "an unmodelled LNK kind drew its alternatives");
}

// The row's own bytes are HTML, so `&`, `<` and `>` are escaped -- and only
// those.  A row that draws no reference is still escaped the same way, which
// is what keeps a `<pre>` block a faithful reproduction.
void a_row_is_escaped_but_not_rewritten() {
  geist::detail::VerbatimRowIR row;
  row.text = "a < b & c > d";
  require(geist::detail::render_verbatim_row(row) ==
              "a &lt; b &amp; c &gt; d",
          "the row was not HTML-escaped: '" +
              geist::detail::render_verbatim_row(row) + "'");
}

} // namespace

int main() {
  a_selector_marks_the_row_it_precedes();
  a_span_past_the_row_is_clamped_not_declined();
  a_picture_selector_is_declined();
  a_footnote_target_may_live_on_another_page();
  a_topic_without_selectors_is_untouched();
  a_lnk_alternative_list_never_reaches_the_row();
  a_cross_book_reference_carries_every_field();
  an_external_reference_resolves_to_its_url();
  an_unparsable_lnk_list_is_declined();
  a_row_is_escaped_but_not_rewritten();
  return 0;
}
