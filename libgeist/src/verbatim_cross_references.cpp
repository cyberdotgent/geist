#include "geist/detail/verbatim_cross_references.hpp"

#include "geist/detail/display_lines.hpp"
#include "geist/detail/selector_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace geist::detail {
namespace {

// The destination spelling every other route uses, so `boo2git` resolves a
// verbatim topic's references through exactly the same book-wide link map.
std::string anchor_destination(const std::string& target) {
  return "](<#" + target + ">)";
}

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

// A Markdown link label ends at the first unescaped `]`, and a `[` inside it
// would nest.  Rather than escaping -- which would change the verbatim
// bytes -- such a span is left plain.
bool label_is_safe(const std::string& label) {
  return label.find('[') == std::string::npos &&
         label.find(']') == std::string::npos;
}

struct PendingLink {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string target;
};

// Where the selector's own opcode and operands are drawn.
const DisplayLineIR* selector_control_line(
    const DecodedLogicalRecordSource& record,
    const ControlSegmentIR& segment) {
  const auto tokens = source_tokens_intersecting_output(
      record.assembled, segment.opcode_range.begin, segment.operand_range.end);
  if (tokens.empty()) return nullptr;
  const auto* first = display_line_of_token(record, tokens.front());
  const auto* last = display_line_of_token(record, tokens.back());
  // A selector whose operands wrap across two display lines does not mark a
  // row this pass can name.
  if (first == nullptr || first != last) return nullptr;
  return first;
}

} // namespace

VerbatimCrossReferenceIR link_verbatim_cross_references(
    const std::vector<DecodedLogicalRecordSource>& sources,
    const std::vector<BestEffortLineIR>& lines,
    const std::vector<std::string>& printed_footnote_anchors) {
  VerbatimCrossReferenceIR result;
  auto& text = result.lines;
  text.reserve(lines.size());
  for (const auto& line : lines) text.push_back(line.text);

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

  std::map<std::size_t, std::vector<PendingLink>> per_line;
  for (const auto& selector : catalog->selectors) {
    if (!selector.canonical_operands || selector.length == 0) continue;
    if (!anchor_shaped(selector.target) || picture_object(selector.target))
      continue;
    // A footnote lives only on the page that prints it.  One this topic does
    // not print is not a destination this topic can prove.
    if (ascii_starts_with_case_insensitive(selector.target, "ftn") &&
        std::find(printed_footnote_anchors.begin(),
                  printed_footnote_anchors.end(),
                  selector.target) == printed_footnote_anchors.end())
      continue;
    const auto record_at = record_index_of.find(selector.logical_record);
    if (record_at == record_index_of.end()) continue;
    const auto record_index = record_at->second;
    const auto& record = sources[record_index];
    if (selector.segment_index >= record.control_segments.size()) continue;
    const auto& segment = record.control_segments[selector.segment_index];
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
    if (selector.column > columns || selector.length > columns - selector.column)
      continue;
    auto begin = row.column_offsets[selector.column];
    auto end = row.column_offsets[selector.column + selector.length];
    // The row's trailing spaces are trimmed away; a span reaching into them
    // covers no text there.
    begin = std::min(begin, row.text.size());
    end = std::min(end, row.text.size());
    while (begin < end && row.text[begin] == ' ') ++begin;
    while (end > begin && row.text[end - 1] == ' ') --end;
    if (begin >= end) continue;
    const auto label = row.text.substr(begin, end - begin);
    if (!label_is_safe(label)) continue;
    per_line[covered].push_back({begin, end, selector.target});
  }

  for (auto& [index, links] : per_line) {
    std::sort(links.begin(), links.end(),
              [](const PendingLink& left, const PendingLink& right) {
                if (left.begin != right.begin) return left.begin < right.begin;
                return left.end < right.end;
              });
    // Two selectors marking overlapping columns cannot both be spelled as
    // Markdown; the later one is dropped rather than guessed at.
    std::vector<PendingLink> accepted;
    for (auto& link : links) {
      if (!accepted.empty() && link.begin < accepted.back().end) continue;
      if (!accepted.empty() && link.begin == accepted.back().begin) continue;
      accepted.push_back(link);
    }
    // Right to left, so an earlier span's offsets stay valid.
    auto& row = text[index];
    for (auto link = accepted.rbegin(); link != accepted.rend(); ++link) {
      row.insert(link->end, anchor_destination(link->target));
      row.insert(link->begin, "[");
      if (!ascii_starts_with_case_insensitive(link->target, "ftn")) continue;
      if (std::find(result.footnote_anchors.begin(),
                    result.footnote_anchors.end(),
                    link->target) == result.footnote_anchors.end())
        result.footnote_anchors.push_back(link->target);
    }
  }
  return result;
}

} // namespace geist::detail
