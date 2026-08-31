#include "geist/detail/ir/topic_header_title.hpp"

#include "geist/detail/container/control_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/ir/figure_block_ir.hpp"

#include <cctype>
#include <cstdint>
#include <vector>

namespace geist::detail {
namespace {

constexpr auto no_token = static_cast<std::size_t>(-1);
constexpr std::uint16_t unmapped_word = 0xffff;

bool box_or_unmapped(const std::uint16_t word) {
  return word == unmapped_word || (word >= 0x2500 && word <= 0x25ff);
}

bool punctuation(const std::uint16_t word) {
  return word < 0x80 && std::ispunct(static_cast<int>(word)) != 0;
}

// Display text of one record-local token: its words minus a leading spacing
// prefix word, which is layout rather than display text (display_lines.hpp).
std::string token_body_text(const DecodedLogicalRecordSource& record,
                            const std::size_t token) {
  const auto& words = record.tokens[token];
  std::string text;
  for (std::size_t index = 0; index < words.size(); ++index) {
    if (index == 0 && words[0] < 4) continue;
    text += figure_display_glyph(words[index]);
  }
  return text;
}

} // namespace

std::optional<std::string> topic_header_title_of_record(
    const DecodedLogicalRecordSource& record) {
  const auto lines = record_display_lines(record);
  if (!lines) return std::nullopt;

  const DisplayLineIR* title_line = nullptr;
  std::size_t opcode_token = no_token;
  for (const auto& line : *lines) {
    for (const auto& cell : display_line_cells(record, line)) {
      if (cell.token == no_token || cell.word == ' ') continue;
      if (ascii_equals_case_insensitive(token_body_text(record, cell.token),
                                        "st")) {
        title_line = &line;
        opcode_token = cell.token;
      }
      break;  // only the line's first visible token opens a control
    }
    if (title_line != nullptr) break;
  }
  if (title_line == nullptr) return std::nullopt;

  // The first token of the next control segment that still carries a control:
  // a segment the display-line pass demoted to display text opens none, so
  // its words belong to this row.
  std::size_t stop = no_token;
  for (const auto& segment : record.control_segments) {
    if (segment.display_text || segment.source_tokens.empty()) continue;
    if (segment.source_tokens.front() > opcode_token) {
      stop = segment.source_tokens.front();
      break;
    }
  }

  std::vector<DisplayLineCellIR> body;
  for (const auto& cell : display_line_cells(record, *title_line)) {
    if (cell.token != no_token) {
      if (cell.token <= opcode_token) continue;
      if (stop != no_token && cell.token >= stop) break;
    }
    body.push_back(cell);
  }

  std::size_t at = 0;
  if (!body.empty() && body[0].word != ' ' && body[0].token != no_token) {
    // A marker slot glued to the opcode is layout: `ST| <title>`.
    std::size_t run = 0;
    while (run < body.size() && body[run].token == body[0].token) ++run;
    bool marker = true;
    for (std::size_t index = 0; index < run; ++index)
      if (!box_or_unmapped(body[index].word) && !punctuation(body[index].word))
        marker = false;
    if (marker) at = run;
  }
  while (at < body.size() && body[at].word == ' ') ++at;

  std::string title;
  for (; at < body.size(); ++at) title += figure_display_glyph(body[at].word);
  return trim_ascii(std::move(title));
}

} // namespace geist::detail
