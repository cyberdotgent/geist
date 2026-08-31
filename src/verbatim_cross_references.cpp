#include "geist/detail/verbatim_cross_references.hpp"

#include "geist/detail/display_lines.hpp"
#include "geist/detail/selector_ir.hpp"
#include "geist/detail/selector_link_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace geist::detail {
namespace {

// An anchor id is an identifier: the opcode of the `SR<id>` control that
// names the destination, so it spells the same alphabet.  Anything else --
// an operand list the parser accepted by coincidence, a spelling carrying
// Markdown or URL punctuation -- is not proven to name a place in the book.
bool anchor_shaped(const std::string& target) {
  if (target.empty()) return false;
  for (const auto ch : target) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte) == 0 && ch != '_' && ch != '-') return false;
  }
  return true;
}

// `cselect <c> <l> PIC<n>` places a stored picture object, not a reference to
// a place in the text; the verbatim route draws no picture and must not
// publish a text link to one.
bool picture_object(const std::string& target) {
  if (!ascii_starts_with_case_insensitive(target, "pic")) return false;
  if (target.size() <= 3) return false;
  for (std::size_t index = 3; index < target.size(); ++index)
    if (std::isdigit(static_cast<unsigned char>(target[index])) == 0)
      return false;
  return true;
}

// Where the selector's own opcode, operands and `LNK` alternatives are drawn.
// All of it has to sit on one display line: a selector whose control wraps
// does not name a single row this pass can bind to.
const DisplayLineIR* selector_control_line(
    const DecodedLogicalRecordSource& record,
    const ControlSegmentIR& segment) {
  auto tokens = source_tokens_intersecting_output(
      record.assembled, segment.opcode_range.begin, segment.operand_range.end);
  const auto alternatives =
      selector_link_alternative_tokens(record, segment);
  tokens.insert(tokens.end(), alternatives.begin(), alternatives.end());
  if (tokens.empty()) return nullptr;
  std::sort(tokens.begin(), tokens.end());
  const auto* first = display_line_of_token(record, tokens.front());
  const auto* last = display_line_of_token(record, tokens.back());
  if (first == nullptr || first != last) return nullptr;
  return first;
}

std::string escape_html_text(const std::string& text) {
  std::string output;
  output.reserve(text.size());
  for (const auto ch : text) {
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

std::string escape_html_attribute(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    switch (ch) {
    case '&':
      output += "&amp;";
      break;
    case '"':
      output += "&quot;";
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

std::string link_destination(const VerbatimLinkIR& link) {
  switch (link.kind) {
  case VerbatimLinkKindIR::in_book:
    return "#" + link.target;
  case VerbatimLinkKindIR::external_url:
    return link.url;
  case VerbatimLinkKindIR::book_contents:
  case VerbatimLinkKindIR::book_heading:
    break;
  }
  // The referenced book is not in this export.  A destination invented here
  // would dangle, and a dangling link is worse than none, so the anchor is
  // kept -- hosted serves one, and the node carries the whole selector -- but
  // it leads nowhere until a backend with a resolver spells it.
  return "#";
}

} // namespace

std::string render_verbatim_row(const VerbatimRowIR& row) {
  std::string output;
  std::size_t cursor = 0;
  for (const auto& link : row.links) {
    if (link.begin < cursor || link.end > row.text.size() ||
        link.begin >= link.end)
      continue;
    output += escape_html_text(row.text.substr(cursor, link.begin - cursor));
    output += "<a href=\"" +
              escape_html_attribute(link_destination(link)) + "\">";
    output += escape_html_text(row.text.substr(link.begin,
                                               link.end - link.begin));
    output += "</a>";
    cursor = link.end;
  }
  output += escape_html_text(row.text.substr(cursor));
  return output;
}

VerbatimCrossReferenceIR link_verbatim_cross_references(
    const std::vector<DecodedLogicalRecordSource>& sources,
    const std::vector<BestEffortLineIR>& lines,
    const std::vector<std::string>& printed_footnote_anchors) {
  VerbatimCrossReferenceIR result;
  result.rows.reserve(lines.size());
  for (const auto& line : lines) result.rows.push_back({line.text, {}});

  const auto catalog = extract_selector_catalog_ir(sources);
  if (!catalog || !verify_selector_catalog_ir(sources, *catalog))
    return result;

  // Where each source display line was emitted, if it was.
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> emitted;
  for (std::size_t index = 0; index < lines.size(); ++index)
    emitted.emplace(
        std::make_pair(lines[index].record_index,
                       lines[index].display_line_index),
        index);

  std::map<std::uint32_t, std::size_t> record_index_of;
  for (std::size_t index = 0; index < sources.size(); ++index)
    record_index_of.emplace(sources[index].logical_record, index);

  std::map<std::size_t, std::vector<VerbatimLinkIR>> per_line;
  for (const auto& selector : catalog->selectors) {
    if (!selector.canonical_operands || selector.length == 0) continue;
    const auto record_at = record_index_of.find(selector.logical_record);
    if (record_at == record_index_of.end()) continue;
    const auto record_index = record_at->second;
    const auto& record = sources[record_index];
    if (selector.segment_index >= record.control_segments.size()) continue;
    const auto& segment = record.control_segments[selector.segment_index];

    VerbatimLinkIR link;
    if (ascii_lower(selector.target) == "lnk") {
      // The `LNK` dialect: the destination is the alternative list, not the
      // operand target.  Fail closed on a list that does not parse, and on
      // an external image -- the verbatim route draws no picture.
      std::vector<std::string> alternatives;
      selector_link_alternative_tokens(record, segment, &alternatives);
      std::string error;
      const auto parsed = parse_selector_link(alternatives, &error);
      if (!parsed) continue;
      if (parsed->kind == SelectorLinkKindIR::external_image) continue;
      link.alternatives = parsed->alternatives;
      link.document_number = parsed->alternatives[3];
      link.document_level = parsed->document_level;
      link.heading_anchor = parsed->alternatives[1];
      link.target = parsed->alternatives[5];
      switch (parsed->kind) {
      case SelectorLinkKindIR::book_contents:
        link.kind = VerbatimLinkKindIR::book_contents;
        break;
      case SelectorLinkKindIR::book_heading:
        link.kind = VerbatimLinkKindIR::book_heading;
        break;
      case SelectorLinkKindIR::external_link:
        link.kind = VerbatimLinkKindIR::external_url;
        link.url = parsed->destination;
        break;
      case SelectorLinkKindIR::external_image:
        continue;
      }
    } else {
      if (!anchor_shaped(selector.target) || picture_object(selector.target))
        continue;
      // A footnote reference may leave the page that prints it.  SC31-6055-1
      // `BIBLIOGRAPHY.1` (DT 19911015203151) carries seven
      // `cselect <col> 4 FTNMERBIB` references and hosted BookServer serves
      // every one of them as `BIBLIOGRAPHY?DT=...#FTNMERBIB` -- the footnote
      // is printed by the *parent* topic, and the reference crosses to it.
      // So the reference is admitted here whatever this topic prints, and
      // where the destination really lives is the book-wide link map's
      // question (`document_link_targets`), with the export unlinking a
      // reference no file in the book answers for.
      link.kind = VerbatimLinkKindIR::in_book;
      link.target = selector.target;
    }

    const auto* control_line = selector_control_line(record, segment);
    if (control_line == nullptr) continue;
    const auto* display = record_display_lines(record);
    if (display == nullptr) continue;
    const auto control_index =
        static_cast<std::size_t>(control_line - display->data());
    // The control's own row must draw nothing of its own -- otherwise the
    // row the selector marks is not the next emitted one.
    if (emitted.count({record_index, control_index}) != 0) continue;

    // The marked row is the next row the verbatim rendering emitted, exactly
    // as a drawn figure binds its `cselect` to the display line it precedes.
    std::size_t covered = lines.size();
    for (std::size_t index = 0; index < lines.size(); ++index) {
      const auto& line = lines[index];
      if (line.record_index < record_index) continue;
      if (line.record_index == record_index &&
          line.display_line_index <= control_index)
        continue;
      covered = index;
      break;
    }
    if (covered == lines.size()) continue;

    const auto& row = lines[covered];
    // `column_offsets` holds one entry per display column plus the end, so a
    // span of `length` columns starting at `column` needs both endpoints.
    if (row.column_offsets.empty()) continue;
    const auto columns = row.column_offsets.size() - 1;
    // The span may name more columns than the row drew.  Hosted clamps it to
    // the row's end (SC09-138 8.1.10.4 marks 60 columns of a 61-column row
    // and serves the whole phrase, DT 19910321130500), which is the only
    // reading available: a row ends where it ends.  A span that *starts*
    // past the row names no text at all and is declined.
    if (selector.column >= columns) continue;
    const auto last = std::min(selector.column + selector.length, columns);
    auto begin = row.column_offsets[selector.column];
    auto end = row.column_offsets[last];
    // The row's trailing spaces are trimmed away; a span reaching into them
    // covers no text there.
    begin = std::min(begin, row.text.size());
    end = std::min(end, row.text.size());
    while (begin < end && row.text[begin] == ' ') ++begin;
    while (end > begin && row.text[end - 1] == ' ') --end;
    if (begin >= end) continue;
    link.begin = begin;
    link.end = end;
    per_line[covered].push_back(std::move(link));
  }

  for (auto& [index, links] : per_line) {
    std::sort(links.begin(), links.end(),
              [](const VerbatimLinkIR& left, const VerbatimLinkIR& right) {
                if (left.begin != right.begin) return left.begin < right.begin;
                return left.end < right.end;
              });
    // Two selectors marking overlapping columns cannot both be spelled as
    // one anchor each; the later one is dropped rather than guessed at.
    auto& accepted = result.rows[index].links;
    for (auto& link : links) {
      if (!accepted.empty() && link.begin < accepted.back().end) continue;
      // The local destination is emitted only for a footnote *this* topic
      // prints.  One printed by another topic already has its anchor there,
      // and emitting a second here would invent a destination the source
      // does not carry.
      if (link.kind == VerbatimLinkKindIR::in_book &&
          ascii_starts_with_case_insensitive(link.target, "ftn") &&
          std::find(printed_footnote_anchors.begin(),
                    printed_footnote_anchors.end(),
                    link.target) != printed_footnote_anchors.end() &&
          std::find(result.footnote_anchors.begin(),
                    result.footnote_anchors.end(),
                    link.target) == result.footnote_anchors.end())
        result.footnote_anchors.push_back(link.target);
      accepted.push_back(std::move(link));
    }
  }
  return result;
}

} // namespace geist::detail
