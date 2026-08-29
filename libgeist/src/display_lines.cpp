#include "geist/detail/display_lines.hpp"

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

void demote_bullet_owned_structural_controls(
    DecodedLogicalRecordSource& record) {
  if (record.control_segments.empty()) return;
  const auto lines = record_display_lines(record);
  if (!lines) return;
  for (auto& segment : record.control_segments) {
    if (segment.kind != BookControlKind::structural ||
        segment.source_tokens.empty())
      continue;
    const auto opcode_token = segment.source_tokens.front();
    for (const auto& line : *lines) {
      if (opcode_token <= line.prefix_token || opcode_token >= line.token_end)
        continue;
      bool bullet = false;
      for (auto token = line.prefix_token + 1; token < opcode_token; ++token) {
        const auto& words = record.ir.tokens[token].decoded_words;
        if (words.size() == 1 && words[0] == list_bullet_word) bullet = true;
      }
      if (!bullet) break;
      segment.kind = BookControlKind::text;
      segment.display_text = true;
      segment.opcode.clear();
      segment.opcode_range = {segment.complete.begin, segment.complete.begin};
      segment.operand_range = segment.opcode_range;
      segment.payload_range = {segment.complete.begin, segment.complete.end};
      break;
    }
  }
}

} // namespace geist::detail
