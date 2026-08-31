// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/drawn_word_conservation.hpp"

#include "geist/detail/container/control_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/layout/layout_ir.hpp"
#include "geist/detail/layout/ownership_ir.hpp"
#include "geist/detail/lowering/topic_identity.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <utility>

namespace geist::detail {

namespace {

bool alphanumeric_byte(const char raw) {
  const auto byte = static_cast<unsigned char>(raw);
  return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z');
}

// Markdown emphasis, code fencing and escaping, which the renderer writes
// *inside* a word: the source's `CLISTs` reaches the file as `` `CLIST`s ``.
// These bytes join rather than separate, on both sides of the comparison, so
// the two agree on where a word ends. Underscore is deliberately not here: it
// separates on both sides, and an escaped `\_` still does once the backslash
// is gone.
bool markup_glue_byte(const char raw) {
  return raw == '*' || raw == '`' || raw == '\\';
}

} // namespace

std::vector<std::string> conservation_words(const std::string& text) {
  std::vector<std::string> words;
  std::string current;
  for (const auto raw : text) {
    if (markup_glue_byte(raw)) continue;
    if (alphanumeric_byte(raw)) {
      current.push_back(ascii_lower_char(raw));
      continue;
    }
    if (!current.empty()) {
      words.push_back(current);
      current.clear();
    }
  }
  if (!current.empty()) words.push_back(current);
  return words;
}

namespace {

using CellKey = std::pair<std::size_t, std::size_t>;

// The cells of one record the ownership ledger attributes to a control's
// opcode or operand. Those cells are markup the reader consumes rather than
// prints, so they are not drawn text even though the framing positions them on
// a display line.
using ControlCells = std::unordered_map<std::uint32_t, std::vector<CellKey>>;

// Control opcodes the corpus stores that the segment decoder does not model,
// each of which owns its whole display line: the reader consumes the line and
// prints none of it.
//
// The decoder classifies the controls the renderer acts on and leaves the rest
// as text, which is the right trade for rendering and the wrong one for a
// conservation check: the operands of an unmodelled control stand inside their
// display line, where display text stands, and would be counted as drawn.
//
//   `si`         a topic's index entry -- the term and its qualifiers, which
//                the reader files into the book index and never prints in the
//                topic body (GX27-3999-00 record 18 lines 0-1,
//                `SI installation materials` / `SI blank diskettes`, neither
//                of which hosted serves in `1.2`).
//   `citerm`     a compiled index topic's term row, `cidelm` its delimiter,
//                `cgpsep` its letter-group separator, `cendindex` its closer
//   `cidelm`     (packet.boo record 369, the generated `INDEX` topic).
//   `cgpsep`
//   `cendindex`
//   `ctoce`      a compiled contents topic's entry row, `ctocdef` the level
//   `ctocdef`    format that topic opens with (XWEBDEMO record 3 lines 12-18,
//                `ctocdef=0 1 0 2` ... `ctocdef=6 0 8`).
//   `c.cp`       page-scope composition directives, spelled like the modelled
//   `c.cc`       `c.sp` and carrying only operands.
//
// An opcode that carries its first operand in the same token is matched on the
// part before the `=`, which is how `ctocdef=0` is stored.
//
// The cost of the list is that a prose line opening with one of these words is
// withdrawn too: SC24-546 `B.2` discusses DBCS shift-out/shift-in and opens
// several lines with `SI`. Closing that gap needs the segment decoder to model
// these controls, not this check to guess harder; until then the check is blind
// to those lines and says so here rather than reporting them as clean.
bool unmodelled_control_opcode(std::string opcode) {
  const auto operand = opcode.find('=');
  if (operand != std::string::npos) opcode.resize(operand);
  static const char* const opcodes[] = {"si",     "citerm",    "cidelm",
                                        "cgpsep", "cendindex", "ctoce",
                                        "ctocdef", "c.cp",     "c.cc"};
  return std::any_of(std::begin(opcodes), std::end(opcodes),
                     [&](const char* candidate) { return opcode == candidate; });
}

// A modelled control whose display line carries printed text after the opcode:
// the title, the menu item's label and the message's own text all stand on the
// opcode's line and all reach the render. Every other control's line is
// operands, which do not.
bool control_line_carries_text(BookControlKind kind) {
  switch (kind) {
  case BookControlKind::title:
  case BookControlKind::menu_item:
  case BookControlKind::message_start:
    return true;
  default:
    return false;
  }
}

// ASCII spelling of one token's own words, without its spacing prefix and
// surrounding spaces, folded to lower case.
std::string token_opcode_text(const LogicalTokenIR& token) {
  TokenWords words;
  for (const auto word : token.decoded_words)
    if (word >= 4) words.push_back(word);
  return ascii_lower(trim_ascii(token_words_to_ascii(words)));
}

// What one record's display-line framing proves about its control segments.
//
// A control's opcode is the first token of its own display line: the encoder
// writes the line's length byte and then the opcode, in every control the
// corpus stores. A segment whose opcode stands anywhere else claims a control
// the framing does not prove -- which is exactly how a word merely spelled like
// a control comes to be deleted from the render with an anchor put in its
// place.
//
// The ownership ledger conserves such a word, but as the phantom control's
// opcode, and attribution to a control is still attribution. So the ledger's
// control attribution is honoured here only where the framing proves the
// control it names; the cells of an unproven one stay drawn text and have to
// survive into the render like any other word.
struct RecordControlFraming {
  // Display lines the reader consumes whole: a proven control whose payload is
  // operands, or an opcode the segment decoder does not model.
  std::vector<bool> owned_lines;
  // Record-local tokens belonging to a proven control segment. A ledger cell
  // outside these is not excluded, whatever the ledger calls it.
  std::vector<bool> proven_tokens;
};

RecordControlFraming control_framing(
    const DecodedLogicalRecordSource& record,
    const std::vector<DisplayLineIR>& lines) {
  RecordControlFraming framing;
  framing.owned_lines.assign(lines.size(), false);
  framing.proven_tokens.assign(record.ir.tokens.size(), false);
  std::unordered_map<std::size_t, std::size_t> line_of_opening_token;
  for (std::size_t index = 0; index < lines.size(); ++index)
    line_of_opening_token.emplace(lines[index].prefix_token + 1, index);
  for (const auto& segment : record.control_segments) {
    if (segment.opcode.empty() || segment.display_text) continue;
    const auto words = decoded_byte_range_to_word_range(record.assembled,
                                                        segment.opcode_range);
    if (words.begin >= record.assembled.sources.size()) continue;
    const auto& source = record.assembled.sources[words.begin];
    if (source.kind != LogicalWordSourceKind::token_word) continue;
    const auto found = line_of_opening_token.find(source.token_index);
    if (found == line_of_opening_token.end()) continue;
    for (const auto token : segment.source_tokens)
      if (token < framing.proven_tokens.size())
        framing.proven_tokens[token] = true;
    if (!control_line_carries_text(segment.kind))
      framing.owned_lines[found->second] = true;
  }
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto opening = lines[index].prefix_token + 1;
    if (opening >= lines[index].token_end) continue;
    if (unmodelled_control_opcode(token_opcode_text(record.ir.tokens[opening])))
      framing.owned_lines[index] = true;
  }
  return framing;
}

// The drawn text of one topic, with the provenance every byte needs for the
// report to name the line a dropped word stood on rather than only count it.
struct DrawnText {
  std::string text;
  // `true` where the byte came from the first token of its display line: the
  // one position a control opcode can occupy.
  std::vector<bool> opening;
  // Index into `sources` of the display line each byte came from.
  std::vector<std::size_t> line;
  std::vector<std::string> sources;
};

DrawnText topic_drawn_text(
    const std::vector<DecodedLogicalRecordSource>& records,
    const OwnershipIR& ownership, std::size_t* unframed_records) {
  ControlCells control;
  for (const auto& cell : ownership.cells) {
    if (cell.disposition != SourceDisposition::control_operand) continue;
    control[cell.logical_record].emplace_back(cell.token_index,
                                              cell.word_index);
  }
  for (auto& entry : control) std::sort(entry.second.begin(), entry.second.end());

  static const std::vector<CellKey> none;
  DrawnText drawn;
  for (const auto& record : records) {
    const auto* lines = record_display_lines(record);
    if (lines == nullptr) {
      ++*unframed_records;
      continue;
    }
    const auto found = control.find(record.logical_record);
    const auto& control_of_record =
        found == control.end() ? none : found->second;
    const auto framing = control_framing(record, *lines);
    for (std::size_t index = 0; index < lines->size(); ++index) {
      const auto& line = (*lines)[index];
      if (framing.owned_lines[index]) continue;
      const auto source = drawn.sources.size();
      drawn.sources.push_back(trim_ascii(display_line_text(record, line)));
      for (const auto& cell : display_line_cells(record, line)) {
        // The ledger's control attribution, accepted only where the framing
        // proves the control it names.
        const auto known_control =
            cell.token != static_cast<std::size_t>(-1) &&
            cell.token < framing.proven_tokens.size() &&
            framing.proven_tokens[cell.token] &&
            std::binary_search(control_of_record.begin(),
                               control_of_record.end(),
                               CellKey{cell.token, cell.word_index});
        const auto opening = cell.token == line.prefix_token + 1;
        const auto piece = known_control
                               ? std::string(" ")
                               : token_words_to_ascii({cell.word});
        drawn.text += piece;
        drawn.opening.insert(drawn.opening.end(), piece.size(), opening);
        drawn.line.insert(drawn.line.end(), piece.size(), source);
      }
      drawn.text.push_back('\n');
      drawn.opening.push_back(false);
      drawn.line.push_back(source);
    }
  }
  return drawn;
}

struct DrawnCounts {
  std::size_t opening = 0;
  std::size_t inside = 0;
  // Display lines this word stands inside, in source order and without
  // repetition: the evidence a report needs to name a drop instead of counting
  // it.
  std::vector<std::size_t> lines;
};

// Cuts the drawn text into the same alphanumeric runs `conservation_words`
// produces, and files each run by the position of its first byte.
std::map<std::string, DrawnCounts> count_drawn_words(const DrawnText& drawn,
                                                     std::size_t* total) {
  std::map<std::string, DrawnCounts> counts;
  std::string current;
  bool opening = false;
  std::size_t line = 0;
  const auto flush = [&]() {
    if (current.empty()) return;
    auto& entry = counts[current];
    (opening ? entry.opening : entry.inside) += 1;
    if (!opening &&
        std::find(entry.lines.begin(), entry.lines.end(), line) ==
            entry.lines.end())
      entry.lines.push_back(line);
    ++*total;
    current.clear();
  };
  for (std::size_t at = 0; at < drawn.text.size(); ++at) {
    if (markup_glue_byte(drawn.text[at])) continue;
    if (!alphanumeric_byte(drawn.text[at])) {
      flush();
      continue;
    }
    if (current.empty()) {
      opening = drawn.opening[at];
      line = drawn.line[at];
    }
    current.push_back(ascii_lower_char(drawn.text[at]));
  }
  flush();
  return counts;
}

} // namespace
} // namespace geist::detail

namespace geist {

detail::DrawnWordConservationIR BooDocument::drawn_word_conservation() const {
  using namespace detail;
  DrawnWordConservationIR report;

  for (const auto& entry : toc_) {
    DrawnWordTopicIR topic;
    topic.id = entry.id;
    topic.route = entry.render_diagnostic().route;

    const auto identity = make_topic_identity(entry);
    if (!topic_identity_has_body(identity)) {
      report.topics.push_back(std::move(topic));
      continue;
    }

    const auto records = decode_logical_record_sources(
        *decode_context_, identity.start_logical_record,
        identity.end_logical_record);
    topic.records = records.size();
    const auto layout = extract_layout_ir(records);
    // The ledger is read for its control attribution only, so a ledger that
    // fails verification is not fatal here: it would cost the check its
    // control exclusions for this topic, which can only add candidate words,
    // never hide one.
    const auto ownership = build_ownership_ir(records, layout);

    const auto drawn =
        topic_drawn_text(records, ownership, &topic.unframed_records);
    const auto drawn_counts = count_drawn_words(drawn, &topic.drawn_words);
    std::map<std::string, std::size_t> emitted_counts;
    for (auto& word : conservation_words(entry.markdown())) {
      ++emitted_counts[word];
      ++topic.emitted_words;
    }
    for (const auto& [word, counts] : drawn_counts) {
      const auto found = emitted_counts.find(word);
      const auto emitted = found == emitted_counts.end() ? 0 : found->second;
      const auto drawn_total = counts.opening + counts.inside;
      if (emitted >= drawn_total) continue;
      const auto shortfall = drawn_total - emitted;
      // Forgive the opening occurrences first: those are the ones that could
      // be a control opcode the segment decoder does not classify. What is
      // left stood inside a display line, where no control opcode ever does.
      const auto forgiven = std::min(shortfall, counts.opening);
      // Name the lines the word stands on, so the report enumerates the drop
      // instead of counting it. Three is enough to recognise the site without
      // turning a repeated word into a transcript of the topic.
      std::string evidence;
      for (std::size_t at = 0; at < counts.lines.size() && at < 3; ++at) {
        if (!evidence.empty()) evidence += " | ";
        evidence += drawn.sources[counts.lines[at]];
      }
      if (counts.lines.size() > 3)
        evidence += " | +" + std::to_string(counts.lines.size() - 3) +
                    " more lines";
      topic.deficits.push_back({word, counts.opening, counts.inside, emitted,
                                shortfall - forgiven, evidence});
      topic.forgiven += forgiven;
      topic.unaccounted += shortfall - forgiven;
    }

    ++report.topics_checked;
    if (topic.unframed_records != 0) ++report.topics_with_unframed_records;
    if (topic.unaccounted != 0) ++report.topics_dropping;
    report.unaccounted_words += topic.unaccounted;
    report.forgiven_words += topic.forgiven;
    report.topics.push_back(std::move(topic));
  }
  return report;
}

} // namespace geist
