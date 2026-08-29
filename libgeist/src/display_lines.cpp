#include "geist/detail/display_lines.hpp"
#include <sstream>

#include "geist/detail/figure_block_ir.hpp"

namespace geist::detail {

std::optional<std::vector<DisplayLineIR>> record_display_lines(
    const DecodedLogicalRecordSource& record) {
  const auto& tokens = record.ir.tokens;
  std::vector<DisplayLineIR> lines;
  std::size_t at = 0;
  while (at < tokens.size()) {
    const auto& prefix = tokens[at];
    if (prefix.byte_range.end != prefix.byte_range.begin + 1)
      return std::nullopt;
    const auto line_end = prefix.byte_range.end + prefix.encoded.value;
    auto end = at + 1;
    while (end < tokens.size() && tokens[end].byte_range.end <= line_end) ++end;
    const auto boundary = end < tokens.size()
                              ? tokens[end].byte_range.begin
                              : record.ir.payload_range.end;
    if (boundary != line_end) return std::nullopt;
    lines.push_back({at, end});
    at = end;
  }
  return lines;
}

namespace {

// Walks the assembled word sources of one display line, calling `visit(word)`
// for every displayed word and `visit(' ')` for every inter-token space the
// assembler inserted inside the line.
template <typename Visit>
void walk_display_line(const DecodedLogicalRecordSource& record,
                       const DisplayLineIR& line, Visit visit) {
  bool started = false;
  bool pending_space = false;
  constexpr auto no_token = static_cast<std::size_t>(-1);
  for (const auto& source : record.assembled.sources) {
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      if (started) pending_space = true;
      continue;
    }
    if (source.token_index <= line.prefix_token) continue;
    if (source.token_index >= line.token_end) break;
    const auto& words = record.tokens[source.token_index];
    if (source.word_index >= words.size() ||
        (source.word_index == 0 && words[0] < 4))
      continue;
    if (pending_space) {
      visit(static_cast<std::uint16_t>(' '), no_token);
      pending_space = false;
    }
    started = true;
    visit(words[source.word_index], source.token_index);
  }
}

} // namespace

std::string display_line_text(const DecodedLogicalRecordSource& record,
                              const DisplayLineIR& line) {
  std::string text;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t) {
                      text += figure_display_glyph(word);
                    });
  return text;
}

std::vector<std::uint16_t> display_line_columns(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line) {
  std::vector<std::uint16_t> columns;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t) {
                      columns.push_back(word);
                    });
  return columns;
}

std::vector<DisplayLineCellIR> display_line_cells(
    const DecodedLogicalRecordSource& record, const DisplayLineIR& line) {
  std::vector<DisplayLineCellIR> cells;
  walk_display_line(record, line,
                    [&](const std::uint16_t word, const std::size_t token) {
                      cells.push_back({word, token});
                    });
  return cells;
}

std::string format_display_line_ir(const DecodedLogicalRecordSource& record,
                                   const DisplayLineIR& line,
                                   const std::size_t index) {
  const auto& prefix = record.ir.tokens[line.prefix_token];
  std::string classes;
  for (const auto word : display_line_columns(record, line)) {
    if (word == ' ')
      classes.push_back('.');
    else if (word >= 0x2500 && word <= 0x25ff)
      classes.push_back('B');
    else if (word == 0xffff)
      classes.push_back('?');
    else
      classes.push_back('x');
  }
  std::ostringstream out;
  out << "line=" << index << " prefix_token=" << line.prefix_token
      << " length=" << prefix.encoded.value << " tokens=["
      << line.prefix_token + 1 << ',' << line.token_end << ") cols="
      << classes.size() << " class='" << classes << "' text='"
      << display_line_text(record, line) << '\'';
  return out.str();
}

namespace {

bool visible_display_token(const LogicalTokenIR& token) {
  for (const auto word : token.decoded_words)
    if (word != ' ') return true;
  return false;
}

} // namespace

void demote_display_line_owned_controls(DecodedLogicalRecordSource& record) {
  if (record.control_segments.empty()) return;
  const auto lines = record_display_lines(record);
  if (!lines) return;
  for (std::size_t index = 0; index < record.control_segments.size(); ++index) {
    auto& segment = record.control_segments[index];
    if (segment.source_tokens.empty()) continue;
    if (segment.kind != BookControlKind::structural &&
        segment.kind != BookControlKind::text)
      continue;
    std::size_t first = record.ir.tokens.size();
    for (const auto token : segment.source_tokens)
      if (visible_display_token(record.ir.tokens[token])) {
        first = token;
        break;
      }
    if (first >= record.ir.tokens.size()) continue;
    const DisplayLineIR* line = nullptr;
    for (const auto& candidate : *lines)
      if (first > candidate.prefix_token && first < candidate.token_end)
        line = &candidate;
    if (line == nullptr) continue;
    bool visible_before = false;
    for (auto token = line->prefix_token + 1; token < first; ++token)
      if (visible_display_token(record.ir.tokens[token])) visible_before = true;
    if (!visible_before) continue;
    // The boundary itself stays where the flattened string put it.  Giving
    // the demoted segment the characters the split consumed (the `,` it fired
    // on, which hosted prints -- SH12-565 3.1.6 `F QH,F XY,SRV=(3,2,2)`) was
    // measured and reverted: reclaiming the gap back to the previous
    // segment's end costs 60 topics, because that gap also carries padding
    // the previous segment's own model relies on.  The dropped separator is a
    // recorded residual instead.
    segment.kind = BookControlKind::text;
    segment.display_text = true;
    segment.opcode.clear();
    segment.opcode_range = {segment.complete.begin, segment.complete.begin};
    segment.operand_range = segment.opcode_range;
    segment.payload_range = {segment.complete.begin, segment.complete.end};
  }
}

} // namespace geist::detail
