// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/figure_block_ir.hpp"

#include "geist/detail/layout/display_lines.hpp"

#include "geist/detail/core/internal.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;
using SegmentKey = std::pair<std::uint32_t, std::size_t>;
using RowKey = std::pair<DisplayRunId, std::size_t>;

struct SegmentView {
  std::size_t record_index = 0;
  const DecodedLogicalRecordSource *record = nullptr;
  const ControlSegmentIR *segment = nullptr;
};

SegmentKey key(const SegmentView &view) {
  return {view.record->logical_record, view.segment->segment_index};
}

FigureSegmentRefIR ref(const SegmentView &view) {
  return {view.record->logical_record, view.segment->segment_index};
}

bool same_ref(const FigureSegmentRefIR &left, const FigureSegmentRefIR &right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index;
}

bool same_ref(const SelectorRefIR &left, const SelectorRefIR &right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.ordinal == right.ordinal;
}

bool same_row(const DocumentSourceRowIR &left,
              const DocumentSourceRowIR &right) {
  return left.display_run == right.display_run &&
         left.row_index == right.row_index;
}

bool same_cell(const FigureSourceCellIR &left,
               const FigureSourceCellIR &right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.token_index == right.token_index &&
         left.word_index == right.word_index && left.word == right.word &&
         left.token_bytes.begin == right.token_bytes.begin &&
         left.token_bytes.end == right.token_bytes.end &&
         left.role == right.role;
}

bool same_block(const FigureSourceBlockIR &left,
                const FigureSourceBlockIR &right) {
  if (!same_ref(left.span.begin, right.span.begin) ||
      !same_ref(left.span.end, right.span.end) ||
      left.span.anchored != right.span.anchored ||
      left.anchor != right.anchor || !same_ref(left.selector, right.selector) ||
      left.target_kind != right.target_kind || left.target != right.target ||
      left.placeholder_text != right.placeholder_text ||
      left.description != right.description ||
      left.additional_pictures.size() != right.additional_pictures.size() ||
      left.spot_anchors.size() != right.spot_anchors.size() ||
      left.body_kind != right.body_kind ||
      left.lines.size() != right.lines.size() ||
      left.index_terms != right.index_terms ||
      left.caption.has_value() != right.caption.has_value() ||
      left.suppressed_rows.size() != right.suppressed_rows.size() ||
      left.cells.size() != right.cells.size())
    return false;
  for (std::size_t index = 0; index < left.additional_pictures.size();
       ++index) {
    const auto &a = left.additional_pictures[index];
    const auto &b = right.additional_pictures[index];
    if (!same_ref(a.selector, b.selector) || a.target_kind != b.target_kind ||
        a.target != b.target || a.placeholder_text != b.placeholder_text)
      return false;
  }
  if (left.caption) {
    if (left.caption->text != right.caption->text ||
        left.caption->rows.size() != right.caption->rows.size())
      return false;
    for (std::size_t index = 0; index < left.caption->rows.size(); ++index)
      if (!same_row(left.caption->rows[index], right.caption->rows[index]))
        return false;
  }
  for (std::size_t index = 0; index < left.suppressed_rows.size(); ++index)
    if (!same_row(left.suppressed_rows[index], right.suppressed_rows[index]))
      return false;
  for (std::size_t index = 0; index < left.lines.size(); ++index) {
    const auto &a = left.lines[index];
    const auto &b = right.lines[index];
    if (a.logical_record != b.logical_record ||
        a.prefix_token != b.prefix_token || a.token_end != b.token_end ||
        a.text != b.text || a.rows.size() != b.rows.size())
      return false;
    for (std::size_t row = 0; row < a.rows.size(); ++row)
      if (!same_row(a.rows[row], b.rows[row]))
        return false;
  }
  for (std::size_t index = 0; index < left.cells.size(); ++index)
    if (!same_cell(left.cells[index], right.cells[index]))
      return false;
  return true;
}

bool same_decline(const FigureBlockDeclineIR &left,
                  const FigureBlockDeclineIR &right) {
  return same_ref(left.begin, right.begin) &&
         left.end.has_value() == right.end.has_value() &&
         (!left.end || same_ref(*left.end, *right.end)) &&
         left.anchor == right.anchor && left.reason == right.reason;
}

std::string opcode_lower(const ControlSegmentIR &segment) {
  return ascii_lower(segment.opcode);
}

bool figure_start(const ControlSegmentIR &segment) {
  return segment.kind == BookControlKind::structural &&
         opcode_lower(segment).rfind("srfig", 0) == 0;
}

// `SRPIC<n>`: the anchor of picture <n>, written by the BUILD 1.3 artwork
// envelope right after `csartdesc <n>`.  Returns "PIC<n>" as hosted names
// the anchor, or nothing for any other structural control.
std::optional<std::string> picture_anchor(const ControlSegmentIR &segment) {
  if (segment.kind != BookControlKind::structural)
    return std::nullopt;
  const auto lower = opcode_lower(segment);
  if (lower.size() <= 5 || lower.rfind("srpic", 0) != 0 ||
      !std::all_of(lower.begin() + 5, lower.end(), [](const unsigned char ch) {
        return std::isdigit(ch) != 0;
      }))
    return std::nullopt;
  return segment.opcode.substr(2);
}

std::string segment_text(const DecodedLogicalRecordSource &record,
                         const OutputRangeIR &range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin >= text.size() || range.end <= range.begin)
    return {};
  return text.substr(range.begin,
                     std::min(range.end, text.size()) - range.begin);
}

// SREFIG arrives either as a structural control (when the decoder isolated
// the opcode, FA1PLMM0 record 38) or as a text segment whose first word is
// "SREFIG" with attached punctuation or a placeholder and the following prose
// ("SREFIG." GG24-4302-00 5.1.8 segment 12; "SREFIG?    The routers ..."
// ACPZMST1 record 57 segment 1).  Returns the decoded byte range of the end
// marker itself, which is all the region owns of that segment.
std::optional<OutputRangeIR> figure_end(const DecodedLogicalRecordSource &record,
                                        const ControlSegmentIR &segment) {
  if (segment.kind == BookControlKind::structural) {
    if (opcode_lower(segment).rfind("srefig", 0) != 0)
      return std::nullopt;
    return segment.opcode_range.begin < segment.opcode_range.end
               ? segment.opcode_range
               : segment.complete;
  }
  if (segment.kind != BookControlKind::text)
    return std::nullopt;
  const auto text = segment_text(record, segment.complete);
  std::size_t cursor = 0;
  while (cursor < text.size() &&
         (std::isspace(static_cast<unsigned char>(text[cursor])) != 0 ||
          text[cursor] == ','))
    ++cursor;
  if (!ascii_starts_with_case_insensitive(text, cursor, "srefig"))
    return std::nullopt;
  auto end = cursor + 6;
  if (end < text.size() &&
      std::isalnum(static_cast<unsigned char>(text[end])) != 0)
    return std::nullopt;
  while (end < text.size() &&
         (text[end] == '.' || text[end] == ',' || text[end] == ';'))
    ++end;
  return OutputRangeIR{segment.complete.begin + cursor,
                       segment.complete.begin + end};
}

struct ExternalImage {
  std::string target;
  // Bytes of the display payload consumed by the alternative list.
  std::size_t metadata_bytes = 0;
};

// LNK selectors carry angle-bracketed alternatives before their display text:
// <IMAGE> <INTERNET> <> </bookmgr/monetcoq.jpg> <> <MONET1> (XWEBDEMO 1.4.1).
std::optional<ExternalImage> external_image(const std::string &payload) {
  std::vector<std::string> alternatives;
  std::size_t cursor = 0;
  while (cursor < payload.size() &&
         std::isspace(static_cast<unsigned char>(payload[cursor])) != 0)
    ++cursor;
  while (cursor < payload.size() && payload[cursor] == '<') {
    const auto close = payload.find('>', cursor + 1);
    if (close == std::string::npos)
      break;
    alternatives.push_back(payload.substr(cursor + 1, close - cursor - 1));
    cursor = close + 1;
    while (cursor < payload.size() &&
           std::isspace(static_cast<unsigned char>(payload[cursor])) != 0)
      ++cursor;
  }
  if (alternatives.empty() || ascii_lower(alternatives.front()) != "image")
    return std::nullopt;
  const auto source = std::find_if(
      alternatives.begin(), alternatives.end(), [](const auto &value) {
        return (!value.empty() && value.front() == '/') ||
               ascii_starts_with_case_insensitive(value, "http://") ||
               ascii_starts_with_case_insensitive(value, "https://") ||
               ascii_starts_with_case_insensitive(value, "ftp://");
      });
  if (source == alternatives.end())
    return std::nullopt;
  return ExternalImage{*source, cursor};
}

// "Figure 20. RSR Components" / "Figure 12-3." / "Figure A-1. Title".
std::optional<std::string> caption_text(const std::string &visible) {
  const auto text = trim_ascii(visible);
  if (!ascii_starts_with_case_insensitive(text, "figure "))
    return std::nullopt;
  std::size_t cursor = 7;
  while (cursor < text.size() && text[cursor] == ' ')
    ++cursor;
  const auto number_begin = cursor;
  while (cursor < text.size() &&
         std::isspace(static_cast<unsigned char>(text[cursor])) == 0)
    ++cursor;
  const auto number = text.substr(number_begin, cursor - number_begin);
  if (number.size() < 2 || number.back() != '.' ||
      std::isalnum(static_cast<unsigned char>(number.front())) == 0)
    return std::nullopt;
  return collapse_ascii_whitespace(text);
}


// Words that can only be box outline, decoder placeholder, spacing or
// separator material.  Anything else is visible text that a figure block
// must not silently suppress.
bool visible_word(std::uint16_t word) {
  // Control/spacing words, the U+2666 decoder placeholder, and the box
  // drawing / block element range (U+2500-U+25FF, GG24-4302-00 5.1.8 token
  // 59 is U+2502) are outline material.
  // 0xFFFF is the decoder's unmapped-word replacement (SG24-204 1.3.2
  // record 34 token 283), which hosted output renders as box filler.
  if (word < 0x20 || word == 0xA0 || word == 0x2666 || word == 0xFFFF ||
      (word >= 0x2500 && word <= 0x25FF))
    return false;
  if (word >= 0x7F)
    return true;
  return std::isalnum(static_cast<int>(word)) != 0;
}


std::vector<std::size_t> word_byte_offsets(
    const AssembledLogicalRecord &assembled) {
  std::vector<std::size_t> offsets(assembled.words.size() + 1);
  for (std::size_t word = 0; word < assembled.words.size(); ++word)
    offsets[word + 1] =
        offsets[word] + token_words_to_ascii({assembled.words[word]}).size();
  return offsets;
}

// Record-local tokens whose assembled output intersects a decoded byte range.
std::vector<std::size_t> tokens_in_bytes(
    const DecodedLogicalRecordSource &record,
    const std::vector<std::size_t> &byte_offsets, const OutputRangeIR &range) {
  std::vector<std::size_t> result;
  for (const auto &span : record.assembled.tokens) {
    if (span.output_begin >= byte_offsets.size() ||
        span.output_end >= byte_offsets.size())
      continue;
    const auto begin = byte_offsets[span.output_begin];
    const auto end = byte_offsets[span.output_end];
    if (begin < range.end && range.begin < end)
      result.push_back(span.token_index);
  }
  return result;
}

struct CellRef {
  CellKey key;
  std::uint16_t word = 0;
};

struct PlaceholderText {
  std::string text;   // "PICTURE 9"
  std::string number; // "9"
};

// Result of classifying one ordered run of source cells (a physical row's
// cells in display order, or a segment lead in token order).  `roles` is
// parallel to the input cells.
struct Classified {
  std::vector<FigureCellRoleIR> roles;
  std::optional<PlaceholderText> placeholder;
  std::optional<std::string> caption;
  std::vector<std::string> index_terms;
  // The row's tail was absorbed into the region's display-line caption.
  bool caption_lines = false;
  bool prose = false;
  std::string prose_text;
};

// The caption of one figure region read off its display lines.
struct RegionCaption {
  std::string text;
  // Record-local tokens of the caption's display lines.
  std::set<std::pair<std::uint32_t, std::size_t>> tokens;
  // Length bytes of every display line of the region: structure, never
  // display text, and never a caption terminator.
  std::set<std::pair<std::uint32_t, std::size_t>> structure;

  bool owns(const CellKey &key) const {
    const std::pair<std::uint32_t, std::size_t> at{std::get<0>(key),
                                                   std::get<1>(key)};
    return tokens.count(at) != 0 || structure.count(at) != 0;
  }

  bool is_structure(const CellKey &key) const {
    return structure.count({std::get<0>(key), std::get<1>(key)}) != 0;
  }
};

// One character per cell so classification positions map back to cells.
char cell_char(std::uint16_t word) {
  if (word >= 0x20 && word <= 0x7E)
    return static_cast<char>(word);
  if (word == 0xA0)
    return ' ';
  return '?';
}

bool skip_char(char ch) {
  return ch == ' ' || ch == '?' || ch == '|' || ch == '_' || ch == '\t';
}

bool has_alnum(const std::string &text) {
  return std::any_of(text.begin(), text.end(), [](const auto ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0;
  });
}

// The material of a figure region is, in any order and any mix per row or
// lead: box outline, "PICTURE n" placeholders, "SI term" subject-index
// entries (invisible in hosted output), and one "Figure N. Title" caption.
// Anything else is prose and the region is declined.
// A slot without a cell is a decoder-inserted space: it advances the text
// but owns no source cell.
using CellSlot = std::optional<CellRef>;

Classified classify_cells(const std::vector<CellSlot> &cells,
                          const RegionCaption *caption = nullptr) {
  Classified result;
  result.roles.assign(cells.size(), FigureCellRoleIR::placeholder_suppressed);
  std::string text;
  text.reserve(cells.size());
  for (const auto &cell : cells)
    text.push_back(cell ? cell_char(cell->word) : ' ');
  const auto lower = ascii_lower(text);

  std::size_t pos = 0;
  while (pos < text.size()) {
    if (skip_char(text[pos])) {
      ++pos;
      continue;
    }
    if (lower.compare(pos, 3, "si ") == 0) {
      auto end = text.find('?', pos);
      if (end == std::string::npos)
        end = text.size();
      result.index_terms.push_back(
          collapse_ascii_whitespace(trim_ascii(text.substr(pos, end - pos))));
      for (auto at = pos; at < end; ++at)
        result.roles[at] = FigureCellRoleIR::index_term;
      pos = end;
      continue;
    }
    if (lower.compare(pos, 8, "picture ") == 0) {
      auto cursor = pos + 8;
      while (cursor < text.size() && text[cursor] == ' ')
        ++cursor;
      const auto digits = cursor;
      while (cursor < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[cursor])) != 0)
        ++cursor;
      if (cursor > digits) {
        PlaceholderText placeholder;
        placeholder.number = text.substr(digits, cursor - digits);
        placeholder.text = figure_picture_placeholder(placeholder.number);
        // "PICTURE 1." keeps the sentence period (DREICMST PREFACE.3).
        if (cursor < text.size() && text[cursor] == '.')
          ++cursor;
        if (cursor == text.size() || skip_char(text[cursor])) {
          if (result.placeholder) {
            result.prose = true;
            result.prose_text = trim_ascii(text.substr(pos));
            return result;
          }
          result.placeholder = std::move(placeholder);
          pos = cursor;
          continue;
        }
      }
    }
    if (const auto caption = caption_text(text.substr(pos))) {
      auto end = text.find_first_of("?|", pos);
      if (end == std::string::npos)
        end = text.size();
      const auto title = collapse_ascii_whitespace(
          trim_ascii(text.substr(pos, end - pos)));
      if (result.caption) {
        result.prose = true;
        result.prose_text = title;
        return result;
      }
      result.caption = title;
      for (auto at = pos; at < end; ++at)
        result.roles[at] = cells[at] && visible_word(cells[at]->word)
                               ? FigureCellRoleIR::caption_content
                               : FigureCellRoleIR::caption_layout;
      pos = end;
      continue;
    }
    if (has_alnum(text.substr(pos))) {
      // The rest of this row is the region's caption when every remaining
      // cell sits on one of the caption's own display lines.  The Layout IR
      // frames a caption row at the marker word before the title and ends it
      // at the length byte that opens the continuation line, so the tail of
      // a caption reaches this point looking like prose.
      if (caption != nullptr) {
        bool owned = true;
        for (auto at = pos; at < cells.size(); ++at)
          if (cells[at] && !caption->owns(cells[at]->key)) {
            owned = false;
            break;
          }
        if (owned) {
          for (auto at = pos; at < cells.size(); ++at)
            if (cells[at])
              result.roles[at] =
                  visible_word(cells[at]->word) &&
                          !caption->is_structure(cells[at]->key)
                      ? FigureCellRoleIR::caption_content
                      : FigureCellRoleIR::caption_layout;
          result.caption_lines = true;
          break;
        }
      }
      result.prose = true;
      result.prose_text =
          collapse_ascii_whitespace(trim_ascii(text.substr(pos)));
      return result;
    }
    break;
  }
  return result;
}

bool body_visible(std::uint16_t word) {
  return word > 0x20 && word != 0xA0 && !(word >= 0x2500 && word <= 0x25FF);
}

bool blank_line(const std::string &text) {
  return std::all_of(text.begin(), text.end(),
                     [](const auto ch) { return ch == ' '; });
}

// A full-width rule without corners, the `frame=rule` figure frame; hosted
// shows it as an empty line (SC09-138 1.3.1, GC23-046 6.2, SC28-1881-05
// 1.6) while a cornered box top/bottom is displayed.
// A dashed rule ("_ _ _ _", SC34-425 1.3.4, drawn from separate one-glyph
// tokens with decoder-inserted spaces) is drawn content and stays.
bool frame_rule_line(const DecodedLogicalRecordSource &record,
                     const DisplayLineIR &line, const std::string &text) {
  for (auto token = line.prefix_token + 1; token < line.token_end; ++token) {
    const auto &words = record.tokens[token];
    for (std::size_t index = 0; index < words.size(); ++index) {
      const auto word = words[index];
      if ((index == 0 && word < 4) || word == 0x2500 || word == ' ' ||
          word == 0xA0)
        continue;
      return false;
    }
  }
  const auto trimmed = trim_ascii(text);
  return !trimmed.empty() &&
         std::all_of(trimmed.begin(), trimmed.end(),
                     [](const auto ch) { return ch == '_'; });
}

// "Figure N. Title" possibly behind a change bar ("| Figure  1-4. ...").
std::optional<std::string> line_caption_text(const std::string &text) {
  auto trimmed = trim_ascii(text);
  while (!trimmed.empty() && (trimmed.front() == '|' || trimmed.front() == ' '))
    trimmed.erase(trimmed.begin());
  return caption_text(trimmed);
}

struct Region {
  std::vector<SegmentView> segments; // begin .. end inclusive, source order
  bool anchored = false;
  std::string anchor;
};

struct Extractor {
  // How one interior line of a drawn figure reads. Declared here rather than
  // inside the member function that classifies them because MSVC cannot
  // resolve a function-local enum from the default member initializer of a
  // function-local class, which is exactly how `Classified` uses it.
  enum class LineKind { blank, control, index, caption, body, spacing };

  const std::vector<DecodedLogicalRecordSource> &records;
  const LayoutIR &layout;
  const OwnershipIR &ownership;
  const SelectorCatalogIR &selectors;
  const std::set<std::string> &resource_ids;
  std::map<std::uint32_t, std::size_t> record_index;
  std::map<std::uint32_t, std::vector<std::size_t>> byte_offsets;
  std::map<SegmentKey, std::vector<const PhysicalRowIR *>> rows_by_segment;
  std::map<RowKey, const PhysicalRowIR *> physical_rows;
  std::map<SegmentKey, std::vector<const SelectorIR *>> selectors_by_segment;
  // Positioned cells per row in display order, boundary (marker) cells first.
  std::map<RowKey, std::vector<const PositionedRowCellIR *>> row_cells;
  std::map<std::uint32_t, std::map<std::size_t, std::size_t>> token_segments;
  std::map<std::uint32_t, std::map<std::size_t, std::size_t>> segment_starts;

  // The segment owning a token, or for an inter-segment separator the
  // segment it follows.
  std::size_t owning_segment(std::uint32_t logical_record,
                             std::size_t token) const {
    const auto exact = token_segments.find(logical_record);
    if (exact != token_segments.end()) {
      const auto found = exact->second.find(token);
      if (found != exact->second.end())
        return found->second;
    }
    const auto starts = segment_starts.find(logical_record);
    if (starts == segment_starts.end() || starts->second.empty())
      return 0;
    auto after = starts->second.upper_bound(token);
    if (after == starts->second.begin())
      return after->second;
    return std::prev(after)->second;
  }

  Extractor(const std::vector<DecodedLogicalRecordSource> &records_,
            const LayoutIR &layout_, const OwnershipIR &ownership_,
            const SelectorCatalogIR &selectors_,
            const std::set<std::string> &resource_ids_)
      : records(records_), layout(layout_), ownership(ownership_),
        selectors(selectors_), resource_ids(resource_ids_) {
    for (std::size_t index = 0; index < records.size(); ++index) {
      const auto &record = records[index];
      record_index[record.logical_record] = index;
      byte_offsets[record.logical_record] = word_byte_offsets(record.assembled);
      auto &segments = token_segments[record.logical_record];
      auto &starts = segment_starts[record.logical_record];
      for (const auto &segment : record.control_segments) {
        for (const auto token : segment.source_tokens)
          segments.emplace(token, segment.segment_index);
        if (!segment.source_tokens.empty())
          starts.emplace(segment.source_tokens.front(), segment.segment_index);
      }
    }
    for (const auto &run : layout.runs)
      for (std::size_t row = 0; row < run.rows.size(); ++row) {
        const auto &physical = run.rows[row];
        rows_by_segment[{physical.logical_record, physical.segment_index}]
            .push_back(&physical);
        physical_rows[{run.id, row}] = &physical;
      }
    for (const auto &selector : selectors.selectors)
      selectors_by_segment[{selector.logical_record, selector.segment_index}]
          .push_back(&selector);
    for (const auto &cell : ownership.row_cells)
      row_cells[{cell.run, cell.row_index}].push_back(&cell);
    for (auto &[row, cells] : row_cells)
      std::stable_sort(cells.begin(), cells.end(),
                       [](const auto *left, const auto *right) {
                         if (left->display_column != right->display_column)
                           return left->display_column < right->display_column;
                         return std::make_tuple(left->logical_record,
                                                left->token_index,
                                                left->word_index) <
                                std::make_tuple(right->logical_record,
                                                right->token_index,
                                                right->word_index);
                       });
  }

  DocumentSourceRowIR row_ref(const PhysicalRowIR &row) const {
    for (const auto &[row_key, physical] : physical_rows)
      if (physical == &row)
        return {row_key.first, row_key.second};
    return {};
  }

  FigureBlockDeclineIR decline(const Region &region, std::string reason,
                               bool terminated = true) const {
    FigureBlockDeclineIR result;
    result.begin = ref(region.segments.front());
    if (terminated)
      result.end = ref(region.segments.back());
    result.anchor = region.anchor;
    result.reason = std::move(reason);
    return result;
  }

  bool picture_selector(const SelectorIR &selector) const {
    return figure_picture_target(selector.target) ||
           (ascii_equals_case_insensitive(selector.target, "lnk") &&
            external_image(selector.display_payload));
  }

  bool has_picture(const Region &region) const {
    for (const auto &view : region.segments) {
      const auto found = selectors_by_segment.find(key(view));
      if (found == selectors_by_segment.end())
        continue;
      for (const auto *selector : found->second)
        if (picture_selector(*selector))
          return true;
    }
    return false;
  }

  // A display line that is one control's opcode and operands and carries no
  // display payload of its own (`cselect 56 12 PIC117`, `cfont 11 3 2 ...`).
  // Such a line shows nothing, so the display line a control names is the
  // first line after it that is not one of these.
  bool control_display_line(const DecodedLogicalRecordSource &record,
                            const DisplayLineIR &line) const {
    const auto first = line.prefix_token + 1;
    if (first >= line.token_end)
      return false;
    const auto segments = token_segments.find(record.logical_record);
    if (segments == token_segments.end())
      return false;
    const auto owner = segments->second.find(first);
    if (owner == segments->second.end())
      return false;
    const auto &segment = record.control_segments[owner->second];
    if (segment.source_tokens.empty() ||
        segment.source_tokens.front() != first ||
        segment.kind == BookControlKind::text)
      return false;
    for (auto token = first; token < line.token_end; ++token) {
      const auto at = token_output_byte(record, token);
      if (at && segment.payload_range.end > segment.payload_range.begin &&
          *at >= segment.payload_range.begin)
        return false;
    }
    return true;
  }

  // The display line a bare picture selector places its image on: the first
  // line after the selector's own control line that shows anything, which is
  // the first line of the next logical record when the control closes its
  // record (SG24-2047-00 5.1 record 190 line 56 `cselect 20 12 PIC113`,
  // record 191 line 0 `       Click on the  PICTURE 113 button.`) and skips
  // the `cfont` line between the selector and its text in the same topic
  // (record 191 lines 37-39).
  std::optional<std::pair<const DecodedLogicalRecordSource *, DisplayLineIR>>
  placement_line(const SegmentView &view) const {
    if (view.segment->source_tokens.empty())
      return std::nullopt;
    // The control's own opcode token names the control line; a selector
    // segment runs on over the display payload, which is already the text
    // the image is placed into.
    const auto opcode = view.segment->source_tokens.front();
    const auto parsed = record_display_lines(*view.record);
    if (!parsed)
      return std::nullopt;
    for (std::size_t index = 0; index < parsed->size(); ++index) {
      const auto &line = (*parsed)[index];
      if (opcode < line.prefix_token || opcode >= line.token_end)
        continue;
      for (auto next = index + 1; next < parsed->size(); ++next)
        if (!control_display_line(*view.record, (*parsed)[next]))
          return std::make_pair(view.record, (*parsed)[next]);
      if (view.record_index + 1 >= records.size())
        return std::nullopt;
      const auto &following = records[view.record_index + 1];
      const auto next_lines = record_display_lines(following);
      if (!next_lines)
        return std::nullopt;
      for (const auto &line : *next_lines)
        if (!control_display_line(following, line))
          return std::make_pair(&following, line);
      return std::nullopt;
    }
    return std::nullopt;
  }

  // True when the placement line carries sentence prose beside the picture's
  // placeholder words: the image is placed *inside* a sentence and the
  // region is no figure at all.  The placeholder must sit at exactly the
  // columns the selector names -- that is what proves the line-relative
  // columns are the ones this text is indexed by -- and what is left of the
  // line once those columns are blanked decides: nothing, or the region's
  // own "Figure N. Title" caption, is a captioned block figure; anything
  // else is prose the picture stands in.
  bool inline_in_prose(const Region &region) const {
    if (region.anchored || region.segments.size() != 1)
      return false;
    const auto &view = region.segments.front();
    const auto found = selectors_by_segment.find(key(view));
    if (found == selectors_by_segment.end() || found->second.size() != 1)
      return false;
    const auto *selector = found->second.front();
    if (!selector->canonical_operands ||
        !figure_picture_target(selector->target))
      return false;
    const auto placed = placement_line(view);
    if (!placed)
      return false;
    // The selector's operands are display *columns*, so the line is read
    // column by column; its UTF-8 projection would misplace them wherever a
    // list glyph or a box rule takes more than one byte (SG24-2047-00 5.1
    // record 191 line 18 opens `       °   Click on the  PICTURE 115 ...`).
    const auto columns = display_line_cells(*placed->first, placed->second);
    std::string text;
    text.reserve(columns.size());
    for (const auto &column : columns)
      text.push_back(cell_char(column.word));
    const auto placeholder = figure_picture_placeholder(
        figure_picture_resource(selector->target));
    const auto end = std::min(selector->column + selector->length, text.size());
    if (selector->column >= end ||
        trim_ascii(text.substr(selector->column, end - selector->column)) !=
            placeholder)
      return false;
    auto rest = text;
    rest.replace(selector->column, end - selector->column,
                 std::string(end - selector->column, ' '));
    if (!has_alnum(rest))
      return false;
    return !line_caption_text(rest).has_value();
  }

  // Lines of one record intersecting [first_token, last_token], in order.
  struct LineView {
    const DecodedLogicalRecordSource *record = nullptr;
    DisplayLineIR line;
  };

  std::vector<DocumentSourceRowIR> rows_in(const DecodedLogicalRecordSource &record,
                                           const DisplayLineIR &line) const {
    std::vector<DocumentSourceRowIR> result;
    for (const auto &[row_key, physical] : physical_rows) {
      if (physical->logical_record != record.logical_record)
        continue;
      if (physical->token_begin < line.token_end &&
          line.prefix_token < physical->token_end)
        result.push_back({row_key.first, row_key.second});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
      return std::make_pair(a.display_run, a.row_index) <
             std::make_pair(b.display_run, b.row_index);
    });
    return result;
  }

  // Output byte at which a record-local token starts.
  std::optional<std::size_t> token_output_byte(
      const DecodedLogicalRecordSource &record, std::size_t token) const {
    const auto &offsets = byte_offsets.at(record.logical_record);
    for (const auto &span : record.assembled.tokens)
      if (span.token_index == token && span.output_begin < offsets.size())
        return offsets[span.output_begin];
    return std::nullopt;
  }

  // The caption of an anchored figure region as its own display lines carry
  // it.
  //
  // A caption is one "Figure N. Title" display line plus the lines that
  // continue it, and hosted BookServer indents every continuation line to
  // the column at which the title starts on the first line:
  //
  //   SH20-918 1.5 record 50 lines 21-24   caption at column 3, title and
  //     continuations at column 13 (hosted DT 19910520154851 prints the four
  //     lines under one another);
  //   DREICMST 1.1.1.2.1 records 61-62 (lines 39-43 and 0-3), same columns,
  //     hosted DT 19911219125856;
  //   ITPPIBOK 5.2 record 169 lines 26-27, same columns, hosted DT
  //     19910628074854;
  //   SC09-2417-00 2.2.4.5 record 236 lines 10-11, caption at column 3 and
  //     the continuation "Length" at column 14, hosted DT 19961114175628.
  //
  // The Layout IR does not delimit a caption.  It opens a row at the marker
  // word in front of the title -- "Figure" itself in IEAC6MST 1.2 record 50
  // token 130, the sentence "." in ITPPIBOK 5.2 record 169 token 192 -- and
  // ends a row at the length byte that opens the continuation line, so a
  // row-by-row reading of the region sees the caption's tail as prose and
  // the caption's head as truncated.  Reading the caption off the display
  // lines keeps it whole; the rows that carry it are then caption rows.
  std::optional<RegionCaption> region_caption(const Region &region) const {
    if (!region.anchored)
      return std::nullopt;
    const auto &begin = region.segments.front();
    const auto &end = region.segments.back();
    if (begin.segment->source_tokens.empty())
      return std::nullopt;
    std::vector<LineView> lines;
    for (auto index = begin.record_index; index <= end.record_index; ++index) {
      const auto &record = records[index];
      const auto parsed = record_display_lines(record);
      if (!parsed)
        return std::nullopt;
      for (const auto &line : *parsed)
        lines.push_back({&record, line});
    }
    const auto srfig_token = begin.segment->source_tokens.front();
    const auto &end_record = *end.record;
    const auto marker = figure_end(end_record, *end.segment);
    if (!marker)
      return std::nullopt;
    const auto end_tokens = tokens_in_bytes(
        end_record, byte_offsets.at(end_record.logical_record), *marker);
    if (end_tokens.empty())
      return std::nullopt;
    const auto marker_first =
        *std::min_element(end_tokens.begin(), end_tokens.end());
    std::size_t first_line = lines.size();
    std::size_t last_line = lines.size();
    for (std::size_t index = 0; index < lines.size(); ++index) {
      const auto &view = lines[index];
      if (view.record == begin.record && view.line.prefix_token < srfig_token &&
          srfig_token < view.line.token_end)
        first_line = index;
      if (view.record == &end_record &&
          view.line.prefix_token < marker_first &&
          marker_first < view.line.token_end)
        last_line = index;
    }
    if (first_line >= lines.size() || last_line >= lines.size() ||
        last_line <= first_line)
      return std::nullopt;

    RegionCaption result;
    for (auto index = first_line; index <= last_line; ++index)
      result.structure.insert({lines[index].record->logical_record,
                               lines[index].line.prefix_token});

    std::size_t caption_at = lines.size();
    std::string head;
    std::size_t title_column = 0;
    for (auto index = first_line + 1; index < last_line; ++index) {
      const auto text =
          display_line_text(*lines[index].record, lines[index].line);
      const auto caption = line_caption_text(text);
      if (!caption)
        continue;
      // The title column: past the leading spaces and change bar, past
      // "Figure", past the number, past the spaces before the title.
      std::size_t at = 0;
      while (at < text.size() && (text[at] == ' ' || text[at] == '|'))
        ++at;
      at += 6; // "Figure"
      while (at < text.size() && text[at] == ' ')
        ++at;
      while (at < text.size() && text[at] != ' ')
        ++at;
      while (at < text.size() && text[at] == ' ')
        ++at;
      if (at >= text.size())
        return std::nullopt;
      caption_at = index;
      head = *caption;
      title_column = at;
      break;
    }
    if (caption_at == lines.size())
      return std::nullopt;

    std::size_t caption_end = caption_at + 1;
    std::string text = head;
    for (auto index = caption_at + 1; index < last_line; ++index) {
      const auto next =
          display_line_text(*lines[index].record, lines[index].line);
      std::size_t start = 0;
      while (start < next.size() && next[start] == ' ')
        ++start;
      if (start >= next.size() || start != title_column)
        break;
      text += " " + trim_ascii(next);
      caption_end = index + 1;
    }
    result.text = collapse_ascii_whitespace(text);
    if (result.text.empty())
      return std::nullopt;
    for (auto index = caption_at; index < caption_end; ++index)
      for (auto token = lines[index].line.prefix_token;
           token < lines[index].line.token_end; ++token)
        result.tokens.insert({lines[index].record->logical_record, token});
    return result;
  }

  std::optional<FigureBlockDeclineIR>
  admit_preformatted(const Region &region, FigureSourceBlockIR &block) const {
    block.body_kind = FigureBodyKindIR::preformatted;
    if (!region.anchored)
      return decline(region, "figure region has no picture selector");
    const auto &begin = region.segments.front();
    const auto &end = region.segments.back();

    // 1. Structural content of the region.  Only SR/menu/message opcodes
    //    are trusted here: the control decoder reads words of a drawn line
    //    ("cforwardlevel", "cselect" in the RMF report of GG24-4302-00
    //    record 262) as controls, so C-controls are judged by whether they
    //    open a display line (step 3).
    for (const auto &view : region.segments) {
      const auto &segment = *view.segment;
      switch (segment.kind) {
      case BookControlKind::table_start:
      case BookControlKind::table_end:
        return decline(region, "figure region contains a table");
      case BookControlKind::menu_start:
      case BookControlKind::menu_item:
      case BookControlKind::menu_end:
        return decline(region, "figure region contains a menu");
      case BookControlKind::message_start:
        return decline(region, "figure region contains a message catalog");
      case BookControlKind::structural:
        // Judged in step 3 by whether the opcode opens a display line: the
        // control decoder reads a *word* of a drawn line as a structural
        // control when it is spelled like one (SH12-565 APPENDIX1.9.5.2.1
        // record 796 line 30 is the body text
        // "     SRVPREF    (server prefix)", and APPENDIX1.9.5.3.1 record
        // 814 line 7 is "     SRVMODE  (server's running mode ...").
        break;
      default:
        break;
      }
    }

    // 2. Display lines of the region's records.
    const auto begin_index = begin.record_index;
    const auto end_index = end.record_index;
    std::vector<LineView> lines;
    for (auto index = begin_index; index <= end_index; ++index) {
      const auto &record = records[index];
      const auto parsed = record_display_lines(record);
      if (!parsed)
        return decline(region, "display line prefixes of record " +
                                   std::to_string(record.logical_record) +
                                   " are misaligned");
      for (const auto &line : *parsed)
        lines.push_back({&record, line});
    }
    // SRFIG opens its line; SREFIG opens its line (after at most blank or
    // comma tokens); the region is the lines in between.
    if (begin.segment->source_tokens.empty())
      return decline(region, "SRFIG has no source tokens");
    const auto srfig_token = begin.segment->source_tokens.front();
    const auto &end_record = *end.record;
    const auto marker = figure_end(end_record, *end.segment);
    std::vector<std::size_t> end_tokens;
    if (marker)
      end_tokens = tokens_in_bytes(end_record, byte_offsets.at(end_record.logical_record), *marker);
    if (end_tokens.empty())
      return decline(region, "SREFIG has no source tokens");
    const auto end_token = *std::max_element(end_tokens.begin(), end_tokens.end());
    const auto marker_first = *std::min_element(end_tokens.begin(), end_tokens.end());
    std::size_t first_line = lines.size();
    std::size_t last_line = lines.size();
    for (std::size_t index = 0; index < lines.size(); ++index) {
      const auto &view = lines[index];
      if (view.record == begin.record && view.line.prefix_token < srfig_token &&
          srfig_token < view.line.token_end) {
        if (view.line.prefix_token + 1 != srfig_token)
          return decline(region, "SRFIG does not open a display line");
        first_line = index;
      }
      if (view.record == &end_record && view.line.prefix_token < marker_first &&
          marker_first < view.line.token_end)
        last_line = index;
    }
    if (first_line >= lines.size() || last_line >= lines.size() ||
        last_line < first_line)
      return decline(region, "figure region lines could not be located");
    {
      const auto &view = lines[last_line];
      for (auto token = view.line.prefix_token + 1; token < marker_first; ++token)
        for (const auto word : end_record.tokens[token])
          if (word >= 4 && word != ' ' && word != ',' && word != 0xA0)
            return decline(region, "SREFIG does not open a display line");
    }

    // Tokens of the SRFIG line beyond the opcode/operands would be body text
    // on the anchor line; none is admitted until hosted shows the shape.
    {
      const auto &view = lines[first_line];
      const auto payload_begin = begin.segment->payload_range.begin;
      for (auto token = srfig_token; token < view.line.token_end; ++token) {
        const auto at = token_output_byte(*view.record, token);
        if (at && *at >= payload_begin)
          return decline(region, "SRFIG line carries payload");
      }
    }

    // 3. Classify the interior lines.
    struct Classified {
      LineKind kind = LineKind::blank;
      std::string text;
    };
    std::vector<Classified> classes(lines.size());
    // `cselect <column> <length> <target>` inside a drawn figure names a
    // span of the display line it precedes (SH20-918 2.1 record 59 line 20
    // `cselect 41 8 FIGTITEM` covers "Figure 4" at column 41 of line 21;
    // SC09-138 8.5.4.5 record 1625 line 12 `cselect 26 3 FIGFREEC3` covers
    // the footnote marker "132" of line 14).  The control opens its own
    // display line, so it is a control line here and the span it covers is
    // resolved once the following lines are classified.
    std::vector<std::pair<std::size_t, const SelectorIR *>> region_selectors;
    std::vector<std::size_t> spot_anchor_lines;
    for (auto index = first_line + 1; index < last_line; ++index) {
      const auto &view = lines[index];
      const auto &record = *view.record;
      const auto &line = view.line;
      auto &result = classes[index];
      if (line.prefix_token + 1 == line.token_end) {
        result.kind = LineKind::blank;
        continue;
      }
      const auto first = line.prefix_token + 1;
      const auto &segments = token_segments.at(record.logical_record);
      const auto owner = segments.find(first);
      if (owner != segments.end()) {
        const auto &segment = record.control_segments[owner->second];
        if (!segment.source_tokens.empty() &&
            segment.source_tokens.front() == first &&
            segment.kind != BookControlKind::text) {
          if (segment.kind == BookControlKind::select) {
            const auto found = selectors_by_segment.find(
                {record.logical_record, segment.segment_index});
            if (found == selectors_by_segment.end() ||
                found->second.size() != 1)
              return decline(region, "drawn figure selector has no typed "
                                     "selector");
            const auto *selector = found->second.front();
            if (!selector->canonical_operands)
              return decline(region, "drawn figure selector operands are not "
                                     "canonical");
            const auto target = ascii_lower(selector->target);
            if (target.rfind("pic", 0) == 0 || target == "lnk")
              return decline(region, "drawn figure contains a " +
                                         selector->target + " selector");
            if (selector->length == 0)
              return decline(region, "drawn figure selector covers no "
                                     "columns");
            region_selectors.emplace_back(index, selector);
            result.kind = LineKind::control;
            continue;
          }
          if (segment.kind == BookControlKind::structural) {
            // A bare `SRSPT<id>` on its own display line is a second anchor
            // hosted opens on the line that follows it.
            if (ascii_lower(segment.opcode).rfind("srspt", 0) != 0)
              return decline(region, "figure region contains structural "
                                     "control " +
                                         segment.opcode);
            for (auto token = first; token < line.token_end; ++token)
              if (std::find(segment.source_tokens.begin(),
                            segment.source_tokens.end(),
                            token) == segment.source_tokens.end())
                return decline(region, "structural control " + segment.opcode +
                                           " does not stand alone on its "
                                           "line");
            spot_anchor_lines.push_back(index);
            block.spot_anchors.push_back({segment.opcode.substr(2),
                                          record.logical_record,
                                          segment.segment_index, false});
            result.kind = LineKind::control;
            continue;
          }
          if (segment.kind != BookControlKind::font &&
              segment.kind != BookControlKind::spacing &&
              segment.kind != BookControlKind::layout_directive)
            return decline(region, "drawn figure contains control " +
                                       segment.opcode);
          for (auto token = first; token < line.token_end; ++token) {
            const auto at = token_output_byte(record, token);
            if (at && *at >= segment.payload_range.begin &&
                segment.payload_range.end > segment.payload_range.begin)
              return decline(region, "control line carries payload");
          }
          result.kind = LineKind::control;
          continue;
        }
      }
      result.text = display_line_text(record, line);
      if (blank_line(result.text))
        result.kind = LineKind::blank;
      else if (ascii_starts_with_case_insensitive(trim_ascii(result.text), "si "))
        result.kind = LineKind::index;
      else if (line_caption_text(result.text))
        result.kind = LineKind::caption;
      else
        result.kind = LineKind::body;
    }

    // 4. Sequence: body lines, then an optional caption with wrapped
    //    continuation lines, then only spacing.
    std::size_t caption_at = lines.size();
    std::size_t caption_end = lines.size(); // exclusive
    for (auto index = first_line + 1; index < last_line; ++index) {
      const auto kind = classes[index].kind;
      if (caption_at == lines.size()) {
        if (kind == LineKind::caption) {
          caption_at = index;
          caption_end = index + 1;
        }
        continue;
      }
      if ((kind == LineKind::control || kind == LineKind::index) &&
          caption_end == index) {
        // A CFONT/CSELECT control or an `SI` subject-index entry may stand
        // between a caption and its wrapped continuation (SH20-918 2.5
        // record 103 lines 13-15, PRG1SORT 1.1.4.3.2 `FIGSELCDF`).
        caption_end = index + 1;
        continue;
      }
      if (kind == LineKind::body && caption_end == index) {
        // Wrapped caption title.  The line is caption material, not body:
        // classifying it as body left its words claimed by the verbatim
        // block while only the caption's first line reached the caption's
        // own slices, so the caption's tail traced to no bytes at all
        // (SC24-5520-00 1.1.26 `Operation` at 0x3e7c7:0x3e7c9).
        classes[index].kind = LineKind::caption;
        caption_end = index + 1;
        continue;
      }
      if (kind == LineKind::body || kind == LineKind::caption)
        return decline(region, "drawn figure has text after its caption '" +
                                   collapse_ascii_whitespace(
                                       trim_ascii(classes[index].text)) +
                                   "'");
    }
    std::size_t body_begin = lines.size();
    std::size_t body_end = lines.size();
    for (auto index = first_line + 1; index < std::min(caption_at, last_line); ++index)
      if (classes[index].kind == LineKind::body) {
        if (body_begin == lines.size())
          body_begin = index;
        body_end = index + 1;
      }
    if (body_begin == lines.size())
      return decline(region, "drawn figure has no body lines");
    if (frame_rule_line(*lines[body_begin].record, lines[body_begin].line,
                        classes[body_begin].text)) {
      classes[body_begin].kind = LineKind::spacing;
      while (body_begin < body_end && classes[body_begin].kind != LineKind::body)
        ++body_begin;
    }
    if (body_end > body_begin &&
        frame_rule_line(*lines[body_end - 1].record, lines[body_end - 1].line,
                        classes[body_end - 1].text)) {
      classes[body_end - 1].kind = LineKind::spacing;
      while (body_end > body_begin && classes[body_end - 1].kind != LineKind::body)
        --body_end;
    }
    if (body_begin >= body_end)
      return decline(region, "drawn figure has no body lines");
    for (auto index = first_line + 1; index < last_line; ++index)
      if (classes[index].kind == LineKind::blank &&
          (index < body_begin || index >= body_end))
        classes[index].kind = LineKind::spacing;

    for (auto index = body_begin; index < body_end; ++index) {
      const auto &view = lines[index];
      if (classes[index].kind != LineKind::body &&
          classes[index].kind != LineKind::blank)
        continue;
      FigurePreformattedLineIR out;
      out.logical_record = view.record->logical_record;
      out.prefix_token = view.line.prefix_token;
      out.token_end = view.line.token_end;
      out.text = classes[index].text;
      out.rows = rows_in(*view.record, view.line);
      out.column_offsets =
          display_line_column_offsets(*view.record, view.line);
      block.lines.push_back(std::move(out));
    }
    if (caption_at < lines.size()) {
      std::string text;
      FigureCaptionIR caption;
      for (auto index = caption_at; index < caption_end; ++index) {
        if (classes[index].kind == LineKind::control ||
            classes[index].kind == LineKind::index)
          continue;
        auto part = trim_ascii(classes[index].text);
        if (index == caption_at)
          part = *line_caption_text(part);
        if (!text.empty())
          text += " ";
        text += collapse_ascii_whitespace(part);
        for (const auto &row : rows_in(*lines[index].record, lines[index].line))
          caption.rows.push_back(row);
      }
      caption.text = text;
      block.caption = std::move(caption);
    }
    for (auto index = first_line + 1; index < last_line; ++index)
      if (classes[index].kind == LineKind::index)
        block.index_terms.push_back(collapse_ascii_whitespace(
            trim_ascii(classes[index].text)));

    for (std::size_t index = 0; index < spot_anchor_lines.size(); ++index) {
      auto next = spot_anchor_lines[index] + 1;
      while (next < last_line && classes[next].kind == LineKind::control)
        ++next;
      block.spot_anchors[index].at_body_start = next == body_begin;
    }

    // 4b. Bind each `cselect` to the display line it precedes.  Hosted
    //     BookServer wraps exactly the covered columns in an anchor: the
    //     span is the link's label, and where the covered line is part of
    //     the caption the link belongs to the caption's inline model.
    for (const auto &[at, selector] : region_selectors) {
      std::size_t covered = lines.size();
      for (auto index = at + 1; index < last_line; ++index)
        if (classes[index].kind == LineKind::body ||
            classes[index].kind == LineKind::caption) {
          covered = index;
          break;
        }
      if (covered >= lines.size())
        return decline(region, "drawn figure selector covers no display line");
      const auto columns =
          display_line_columns(*lines[covered].record, lines[covered].line);
      if (selector->column + selector->length > columns.size())
        return decline(region, "drawn figure selector span is outside its "
                               "display line");
      FigureLinkIR link;
      link.selector = {selector->logical_record, selector->segment_index,
                       selector->selector_ordinal};
      link.target = selector->target;
      link.logical_record = lines[covered].record->logical_record;
      link.line_prefix_token = lines[covered].line.prefix_token;
      link.column = selector->column;
      link.length = selector->length;
      for (auto column = selector->column;
           column < selector->column + selector->length; ++column)
        link.label += figure_display_glyph(columns[column]);
      link.label = trim_ascii(link.label);
      if (link.label.empty())
        return decline(region, "drawn figure selector covers no text");
      {
        const auto &record = *lines[at].record;
        const auto &segment =
            record.control_segments[selector->segment_index];
        if (segment.source_tokens.empty())
          return decline(region, "drawn figure selector has no source tokens");
        const auto first_token = segment.source_tokens.front();
        const auto last_token = lines[at].line.token_end;
        link.source.logical_record = record.logical_record;
        link.source.segment_index = selector->segment_index;
        link.source.token_begin = first_token;
        link.source.token_end = last_token;
        if (first_token < record.ir.tokens.size() &&
            last_token - 1 < record.ir.tokens.size()) {
          link.source.byte_begin = record.ir.tokens[first_token].byte_range.begin;
          link.source.byte_end = record.ir.tokens[last_token - 1].byte_range.end;
        }
      }
      const auto in_caption = caption_at < lines.size() &&
                              covered >= caption_at && covered < caption_end &&
                              classes[covered].kind == LineKind::caption;
      if (!in_caption) {
        block.body_links.push_back(std::move(link));
        continue;
      }
      // The caption's text collapses each line's runs of spaces, so the
      // label is located in the caption by its own collapsed spelling.
      const auto label = collapse_ascii_whitespace(link.label);
      const auto begin = block.caption->text.find(label);
      if (begin == std::string::npos ||
          block.caption->text.find(label, begin + 1) != std::string::npos)
        return decline(region, "drawn figure caption link '" + label +
                                   "' is not uniquely placed");
      FigureCaptionIR::Span span;
      span.begin = begin;
      span.end = begin + label.size();
      span.link = std::move(link);
      block.caption->links.push_back(std::move(span));
    }
    if (block.caption) {
      auto &links = block.caption->links;
      std::sort(links.begin(), links.end(),
                [](const auto &left, const auto &right) {
                  return left.begin < right.begin;
                });
      for (std::size_t index = 1; index < links.size(); ++index)
        if (links[index].begin < links[index - 1].end)
          return decline(region, "drawn figure caption links overlap");
    }

    // 5. Claim every source cell of the region exactly once.
    std::map<std::pair<std::uint32_t, std::size_t>, LineKind> token_kind;
    std::map<std::pair<std::uint32_t, std::size_t>, bool> token_prefix;
    for (auto index = first_line; index <= last_line; ++index) {
      const auto &view = lines[index];
      const auto record = view.record->logical_record;
      token_prefix[{record, view.line.prefix_token}] = true;
      auto kind = classes[index].kind;
      if (index == first_line)
        kind = LineKind::control;
      for (auto token = view.line.prefix_token + 1; token < view.line.token_end; ++token)
        token_kind[{record, token}] = kind;
    }
    const auto begin_token = lines[first_line].line.prefix_token;
    for (const auto &cell : ownership.cells) {
      const auto found = record_index.find(cell.logical_record);
      if (found == record_index.end())
        continue;
      const auto index = found->second;
      if (index < begin_index || index > end_index)
        continue;
      if (index == begin_index && cell.token_index < begin_token)
        continue;
      if (index == end_index && cell.token_index > end_token)
        continue;
      const auto &record = records[index];
      FigureSourceCellIR claimed;
      claimed.logical_record = cell.logical_record;
      claimed.segment_index = owning_segment(cell.logical_record, cell.token_index);
      claimed.token_index = cell.token_index;
      claimed.word_index = cell.word_index;
      claimed.word = cell.word;
      if (cell.token_index < record.ir.tokens.size())
        claimed.token_bytes = record.ir.tokens[cell.token_index].byte_range;
      const auto spacing_prefix = cell.word_index == 0 && cell.word < 4;
      if (token_prefix.count({cell.logical_record, cell.token_index}) != 0) {
        claimed.role = FigureCellRoleIR::line_prefix;
      } else if (index == end_index && cell.token_index >= lines[last_line].line.prefix_token + 1) {
        claimed.role = FigureCellRoleIR::boundary;
      } else {
        const auto kind = token_kind.find({cell.logical_record, cell.token_index});
        if (kind == token_kind.end())
          return decline(region, "figure region cell is outside its lines");
        switch (kind->second) {
        case LineKind::control:
          claimed.role = FigureCellRoleIR::control;
          break;
        case LineKind::index:
          claimed.role = FigureCellRoleIR::index_term;
          break;
        case LineKind::caption:
          claimed.role = spacing_prefix ? FigureCellRoleIR::control
                         : body_visible(cell.word) ? FigureCellRoleIR::caption_content
                                                   : FigureCellRoleIR::caption_layout;
          break;
        case LineKind::body:
        case LineKind::blank:
          claimed.role = spacing_prefix ? FigureCellRoleIR::control
                         : body_visible(cell.word) ? FigureCellRoleIR::body_content
                                                   : FigureCellRoleIR::body_layout;
          break;
        case LineKind::spacing:
          claimed.role = FigureCellRoleIR::spacing;
          break;
        }
      }
      block.cells.push_back(std::move(claimed));
    }
    if (block.cells.empty())
      return decline(region, "figure region owns no source cells");
    return std::nullopt;
  }

  std::optional<FigureBlockDeclineIR> admit(const Region &region,
                                            FigureSourceBlockIR &block) const {
    const auto &begin = region.segments.front();
    const auto &end = region.segments.back();
    block.span.begin = ref(begin);
    block.span.end = ref(end);
    block.span.anchored = region.anchored;
    block.anchor = region.anchor;

    // A ruled table anywhere inside the region makes it the table family's
    // frame, whatever else the region carries.  This is decided before any
    // other structural content because a captioned frame opens an inner
    // `SRFIG<tbl>` one segment ahead of its `SRTBL<tbl>` (DREICMST 1.6.1
    // records 137-140), and reading that opener as an interior structural
    // control declined the frame the table family plans.
    for (const auto &view : region.segments)
      if (view.segment->kind == BookControlKind::table_start ||
          view.segment->kind == BookControlKind::table_end)
        return decline(region, "figure region contains a table");

    // ASCII-art and CFONT-boxed figures carry no picture at all: their body
    // is reproduced line for line instead.
    if (!has_picture(region))
      return admit_preformatted(region, block);

    // 1. Structural content of the region.
    const SelectorIR *picture = nullptr;
    std::string description_target;
    for (std::size_t index = 0; index < region.segments.size(); ++index) {
      const auto &view = region.segments[index];
      const auto &segment = *view.segment;
      const auto interior =
          !region.anchored ||
          (index != 0 && index + 1 != region.segments.size());
      switch (segment.kind) {
      case BookControlKind::table_start:
      case BookControlKind::table_end:
        return decline(region, "figure region contains a table");
      case BookControlKind::menu_start:
      case BookControlKind::menu_item:
      case BookControlKind::menu_end:
        return decline(region, "figure region contains a menu");
      case BookControlKind::message_start:
        return decline(region, "figure region contains a message catalog");
      case BookControlKind::topic_start:
      case BookControlKind::topic_number:
      case BookControlKind::parent:
      case BookControlKind::forward_level:
      case BookControlKind::back_level:
      case BookControlKind::summary:
      case BookControlKind::heading_level:
      case BookControlKind::source_file:
      case BookControlKind::title:
        return decline(region, "figure region crosses a topic boundary");
      case BookControlKind::unknown:
        return decline(region, "figure region contains an unknown control");
      case BookControlKind::structural:
        if (interior) {
          // The picture's own anchor (SG24-4815-01 1.1 `SRPIC1`) is the one
          // structural control the envelope carries; it is bound to the
          // picture once the selectors are known.
          const auto anchor = picture_anchor(segment);
          if (!anchor)
            return decline(region, "figure region contains structural "
                                   "control " +
                                       segment.opcode);
          block.spot_anchors.push_back({*anchor, segment.logical_record,
                                        segment.segment_index, true});
        }
        break;
      case BookControlKind::art_start:
      case BookControlKind::art_end:
      case BookControlKind::art_description_end:
        // Envelope boundaries: opcode cells only.
        break;
      case BookControlKind::art_description_start: {
        // `csartdesc <n>` names the picture the description belongs to.
        const auto operand = trim_ascii(
            segment_text(*view.record, segment.operand_range));
        if (operand.empty())
          return decline(region, "art description names no picture");
        description_target = "pic" + ascii_lower(operand);
        break;
      }
      case BookControlKind::art_description: {
        // One `cartdesc` line of the description; a bare `cartdesc` is a
        // blank line (SG24-4815-01 1.1 record 25 line 41).
        const auto text = collapse_ascii_whitespace(
            trim_ascii(segment_text(*view.record, segment.payload_range)));
        if (text.empty())
          break;
        if (!block.description.empty())
          block.description += ' ';
        block.description += text;
        break;
      }
      case BookControlKind::select: {
        const auto found = selectors_by_segment.find(key(view));
        if (found == selectors_by_segment.end() || found->second.size() != 1)
          return decline(region, "selector segment has no typed selector");
        const auto *selector = found->second.front();
        if (!selector->canonical_operands)
          return decline(region, "selector operands are not canonical");
        if (selector->inside_table)
          return decline(region, "picture selector is table-owned");
        if (figure_picture_target(selector->target)) {
          // Several picture selectors under one caption are one figure
          // (SC26-457 3.2.1 PIC1 + PIC2, B.1.3 PIC4 + PIC5, SC34-425 2.1.2
          // PIC21 + PIC22; hosted stacks the images under one caption).
          const auto target = figure_picture_resource(selector->target);
          if (resource_ids.count(ascii_lower(target)) == 0)
            return decline(region, "picture resource " + target +
                                       " is not in the resource catalog");
          if (picture != nullptr) {
            if (block.target_kind != FigureTargetKindIR::book_resource)
              return decline(region, "figure region contains several "
                                     "pictures");
            block.additional_pictures.push_back(
                {{selector->logical_record, selector->segment_index,
                  selector->selector_ordinal},
                 FigureTargetKindIR::book_resource, target, {}});
            break;
          }
          picture = selector;
          block.target_kind = FigureTargetKindIR::book_resource;
          block.target = target;
        } else if (ascii_equals_case_insensitive(selector->target, "lnk")) {
          const auto external = external_image(selector->display_payload);
          if (!external)
            return decline(region, "LNK selector in figure is not an image");
          if (picture != nullptr)
            return decline(region, "figure region contains several pictures");
          picture = selector;
          block.target_kind = FigureTargetKindIR::external_image;
          block.target = external->target;
        } else {
          return decline(region, "figure region contains a non-picture "
                                 "selector " +
                                     selector->target);
        }
        block.selector = {selector->logical_record, selector->segment_index,
                          selector->selector_ordinal};
        break;
      }
      default:
        break;
      }
    }
    if (picture == nullptr)
      return decline(region, "figure region has no picture selector");

    // The artwork envelope names its picture twice, by the anchor and by the
    // description opener; both must be the region's picture.  A description
    // of a picture after the first has no place to go and declines.
    const auto main_picture = "pic" + ascii_lower(block.target);
    for (const auto &spot : block.spot_anchors)
      if (ascii_lower(spot.id) != main_picture)
        return decline(region, "anchor " + spot.id +
                                   " names a different picture than " +
                                   block.target);
    if (!description_target.empty() && description_target != main_picture)
      return decline(region, "art description names a different picture "
                             "than " +
                                 block.target);
    if (!block.description.empty() && description_target.empty())
      return decline(region, "art description lines outside an envelope");

    // 2. Classify every physical row and every segment lead (the visible
    //    material of a segment before its first row) inside the region.
    std::map<CellKey, FigureCellRoleIR> cell_roles;
    const PhysicalRowIR *caption_row = nullptr;
    const auto line_caption = region_caption(region);
    std::vector<DocumentSourceRowIR> line_caption_rows;
    const auto picture_key = SegmentKey{picture->logical_record,
                                        picture->segment_index};
    std::size_t metadata_end = 0;
    if (block.target_kind == FigureTargetKindIR::external_image)
      metadata_end = picture->payload_range.begin +
                     external_image(picture->display_payload)->metadata_bytes;

    struct PlaceholderSlot {
      const std::string *target = nullptr;
      FigureTargetKindIR kind = FigureTargetKindIR::book_resource;
      std::string *placeholder = nullptr;
    };
    std::vector<PlaceholderSlot> placeholder_slots;
    placeholder_slots.push_back(
        {&block.target, block.target_kind, &block.placeholder_text});
    for (auto &extra : block.additional_pictures)
      placeholder_slots.push_back(
          {&extra.target, extra.target_kind, &extra.placeholder_text});
    std::size_t placeholders_seen = 0;

    const auto apply = [&](const Classified &classified,
                           const std::vector<CellSlot> &cells,
                           const DocumentSourceRowIR *row)
        -> std::optional<FigureBlockDeclineIR> {
      if (classified.placeholder) {
        // One placeholder per picture, in source order.
        if (placeholders_seen >= placeholder_slots.size())
          return decline(region, "figure region has several placeholders");
        auto &slot = placeholder_slots[placeholders_seen];
        if (slot.kind == FigureTargetKindIR::book_resource &&
            ascii_lower(classified.placeholder->number) !=
                ascii_lower(*slot.target))
          return decline(region, "placeholder PICTURE " +
                                     classified.placeholder->number +
                                     " names a different picture than " +
                                     *slot.target);
        *slot.placeholder = classified.placeholder->text;
        ++placeholders_seen;
      }
      if (classified.caption) {
        if (block.caption)
          return decline(region, "figure region has several captions");
        block.caption = FigureCaptionIR{*classified.caption, {}, {}};
        if (row != nullptr)
          block.caption->rows.push_back(*row);
      }
      for (const auto &term : classified.index_terms)
        block.index_terms.push_back(term);
      for (std::size_t index = 0; index < cells.size(); ++index)
        if (cells[index])
          cell_roles[cells[index]->key] = classified.roles[index];
      if (row != nullptr && !classified.caption)
        block.suppressed_rows.push_back(*row);
      return std::nullopt;
    };

    for (std::size_t index = 0; index < region.segments.size(); ++index) {
      const auto &view = region.segments[index];
      const auto &segment = *view.segment;
      const auto &record = *view.record;
      const auto segment_key = key(view);
      const auto boundary_segment =
          region.anchored && (index == 0 || index + 1 == region.segments.size());
      std::vector<const PhysicalRowIR *> rows;
      if (!boundary_segment) {
        const auto found = rows_by_segment.find(segment_key);
        if (found != rows_by_segment.end())
          rows = found->second;
      }

      // Segment lead.
      if (!boundary_segment &&
          (segment.kind == BookControlKind::text ||
           segment.kind == BookControlKind::select ||
           segment.kind == BookControlKind::font)) {
        const auto &offsets = byte_offsets.at(record.logical_record);
        auto lead_begin = segment.kind == BookControlKind::text
                              ? segment.complete.begin
                              : segment.payload_range.begin;
        if (segment_key == picture_key && metadata_end > lead_begin)
          lead_begin = metadata_end;
        const auto lead_end_token =
            rows.empty() ? std::numeric_limits<std::size_t>::max()
                         : rows.front()->token_begin;
        std::set<std::size_t> lead_tokens;
        for (const auto token : segment.source_tokens) {
          if (token >= lead_end_token || token >= record.tokens.size())
            continue;
          const auto span = std::find_if(
              record.assembled.tokens.begin(), record.assembled.tokens.end(),
              [&](const auto &candidate) {
                return candidate.token_index == token;
              });
          if (span == record.assembled.tokens.end() ||
              offsets[span->output_begin] < lead_begin)
            continue;
          lead_tokens.insert(token);
        }
        // Walk the assembled output so decoder-inserted spaces between lead
        // tokens keep the words apart.
        std::vector<CellSlot> lead;
        bool inside = false;
        for (const auto &source : record.assembled.sources) {
          if (source.kind == LogicalWordSourceKind::inserted_space) {
            if (inside)
              lead.push_back(std::nullopt);
            continue;
          }
          if (lead_tokens.count(source.token_index) == 0) {
            if (inside && !lead.empty() && lead.back())
              lead.push_back(std::nullopt);
            inside = false;
            continue;
          }
          inside = true;
          const auto &words = record.tokens[source.token_index];
          if (source.word_index >= words.size() ||
              (source.word_index == 0 && words[0] < 4))
            continue;
          lead.push_back(CellRef{{record.logical_record, source.token_index,
                                  source.word_index},
                                 words[source.word_index]});
        }
        while (!lead.empty() && !lead.back())
          lead.pop_back();
        if (!lead.empty()) {
          const auto classified = classify_cells(lead);
          if (classified.prose)
            return decline(region, "figure region contains prose segment '" +
                                       classified.prose_text + "'");
          if (auto declined = apply(classified, lead, nullptr))
            return declined;
        }
      }

      // Physical rows.
      for (const auto *row : rows) {
        const auto row_key = row_ref(*row);
        const RowKey rk{row_key.display_run, row_key.row_index};
        std::vector<CellSlot> cells;
        const auto owned = row_cells.find(rk);
        std::optional<std::size_t> previous_column;
        if (owned != row_cells.end())
          for (const auto *cell : owned->second) {
            if (cell->display_column && previous_column &&
                *cell->display_column > *previous_column + 1)
              cells.push_back(std::nullopt);
            if (cell->display_column)
              previous_column = cell->display_column;
            // A marker-slot (boundary) cell is row geometry, not text.
            cells.push_back(CellRef{{cell->logical_record, cell->token_index,
                                     cell->word_index},
                                    cell->role == RowCellRole::boundary
                                        ? std::uint16_t{' '}
                                        : cell->word});
          }
        auto classified = classify_cells(
            cells, line_caption ? &*line_caption : nullptr);
        if (classified.caption_lines)
          line_caption_rows.push_back(row_key);
        if (classified.prose && caption_row != nullptr &&
            row->run == caption_row->run &&
            (row->start == PhysicalRowStartKind::placeholder_wrap ||
             row->start == PhysicalRowStartKind::record_continuation)) {
          // Soft-wrapped continuation of the caption title.
          block.caption->text += " " + classified.prose_text;
          block.caption->rows.push_back(row_key);
          for (const auto &cell : cells)
            if (cell)
              cell_roles[cell->key] = visible_word(cell->word)
                                          ? FigureCellRoleIR::caption_content
                                          : FigureCellRoleIR::caption_layout;
          continue;
        }
        if (classified.prose)
          return decline(region, "figure region contains prose row '" +
                                     classified.prose_text + "'");
        if (auto declined = apply(classified, cells,
                                  classified.caption_lines ? nullptr
                                                           : &row_key))
          return declined;
        if (classified.caption)
          caption_row = row;
      }
    }

    // Rows that carry the caption's display lines but not its "Figure N."
    // head hand the caption its whole text and their own rows.
    if (!line_caption_rows.empty()) {
      if (!block.caption)
        block.caption = FigureCaptionIR{line_caption->text, {}, {}};
      else
        block.caption->text = line_caption->text;
      for (const auto &row : line_caption_rows)
        block.caption->rows.push_back(row);
    }

    // 3. Claim every source cell inside the region exactly once.
    const auto &end_record = *end.record;
    const auto begin_token = begin.segment->source_tokens.empty()
                                 ? std::size_t{0}
                                 : begin.segment->source_tokens.front();
    std::vector<std::size_t> end_tokens;
    if (region.anchored) {
      const auto &offsets = byte_offsets.at(end_record.logical_record);
      const auto marker = figure_end(end_record, *end.segment);
      if (marker)
        end_tokens = tokens_in_bytes(end_record, offsets, *marker);
      if (end_tokens.empty())
        return decline(region, "SREFIG has no source tokens");
    } else {
      end_tokens = end.segment->source_tokens;
    }
    const auto end_token = *std::max_element(end_tokens.begin(),
                                             end_tokens.end());
    const std::set<std::size_t> boundary_tokens(end_tokens.begin(),
                                                end_tokens.end());
    const auto begin_index = begin.record_index;
    const auto end_index = end.record_index;

    std::map<SegmentKey, const SegmentView *> region_segments;
    for (const auto &view : region.segments)
      region_segments[key(view)] = &view;

    for (const auto &cell : ownership.cells) {
      const auto found = record_index.find(cell.logical_record);
      if (found == record_index.end())
        continue;
      const auto index = found->second;
      if (index < begin_index || index > end_index)
        continue;
      if (index == begin_index && cell.token_index < begin_token)
        continue;
      if (index == end_index && cell.token_index > end_token)
        continue;
      const auto &record = records[index];
      FigureSourceCellIR claimed;
      claimed.logical_record = cell.logical_record;
      claimed.segment_index =
          owning_segment(cell.logical_record, cell.token_index);
      claimed.token_index = cell.token_index;
      claimed.word_index = cell.word_index;
      claimed.word = cell.word;
      if (cell.token_index < record.ir.tokens.size())
        claimed.token_bytes = record.ir.tokens[cell.token_index].byte_range;

      const auto classified = cell_roles.find(
          {cell.logical_record, cell.token_index, cell.word_index});
      if (classified != cell_roles.end()) {
        claimed.role = classified->second;
      } else if (cell.run != 0) {
        return decline(region, "figure region claims a cell of a row it "
                               "does not own");
      } else if (cell.disposition == SourceDisposition::control_operand) {
        claimed.role = FigureCellRoleIR::control;
      } else if (region.anchored && index == end_index &&
                 boundary_tokens.count(cell.token_index) != 0) {
        claimed.role = FigureCellRoleIR::boundary;
      } else {
        const auto &segments = token_segments.at(cell.logical_record);
        const auto owner = segments.find(cell.token_index);
        const SegmentView *view = nullptr;
        if (owner != segments.end()) {
          const auto region_owner = region_segments.find(
              {cell.logical_record, owner->second});
          if (region_owner != region_segments.end())
            view = region_owner->second;
        }
        const auto allowed = !visible_word(cell.word);
        if (view != nullptr &&
            view->segment->kind == BookControlKind::art_description) {
          // The `cartdesc` line's text, carried as the block's description.
          claimed.role = FigureCellRoleIR::description;
        } else if (view != nullptr &&
                   (view->segment->kind ==
                        BookControlKind::layout_directive ||
                    view->segment->kind == BookControlKind::spacing)) {
          claimed.role = FigureCellRoleIR::control;
        } else if (view != nullptr && key(*view) == picture_key &&
                   metadata_end != 0) {
          const auto &offsets = byte_offsets.at(cell.logical_record);
          const auto &spans = record.assembled.tokens;
          const auto span = std::find_if(
              spans.begin(), spans.end(), [&](const auto &candidate) {
                return candidate.token_index == cell.token_index;
              });
          const auto begin_byte = span == spans.end()
                                      ? std::size_t{0}
                                      : offsets[span->output_begin];
          if (begin_byte < metadata_end)
            claimed.role = FigureCellRoleIR::control;
          else if (allowed)
            claimed.role = FigureCellRoleIR::placeholder_suppressed;
          else
            return decline(region, "picture selector carries visible text "
                                   "outside any row");
        } else if (allowed) {
          claimed.role = FigureCellRoleIR::placeholder_suppressed;
        } else {
          std::ostringstream where;
          where << cell.logical_record << ':' << cell.token_index << " '"
                << token_words_to_ascii(record.tokens[cell.token_index])
                << "' word=0x" << std::hex << cell.word;
          return decline(region, "figure box contains visible text outside "
                                 "any row at " +
                                     where.str());
        }
      }
      block.cells.push_back(std::move(claimed));
    }
    if (block.cells.empty())
      return decline(region, "figure region owns no source cells");
    return std::nullopt;
  }

  FigureBlocksIR run() const {
    FigureBlocksIR result;
    // Figure regions nest.  A captioned figure that frames a ruled table is
    // written as an outer `SRFIG<caption>` around an inner
    // `SRFIG<tbl> ... SRTBL<tbl> ... SRETBL SREFIG`, closed by a second
    // `SREFIG` -- DREICMST 1.2.1 records 79-84 spell
    // `SRFIGLOGPROC`, `SRFIGXXX`, `SRTBLXXX`, `SRETBL`, `SREFIG`, `SREFIG`,
    // and hosted DT 19911219125856 serves exactly that as
    // `<a name="FIGLOGPROC">   split=yes.</a>` followed by
    // `<a name="FIGXXX"><a name="TBLXXX">` on the table's top rule.  Reading
    // the inner opener as an unterminated outer region lost both.
    std::vector<Region> open;
    bool table_open = false;
    const auto close = [&](Region region) {
      FigureSourceBlockIR block;
      if (auto declined = admit(region, block))
        result.declined.push_back(std::move(*declined));
      else
        result.blocks.push_back(std::move(block));
    };
    for (std::size_t index = 0; index < records.size(); ++index) {
      const auto &record = records[index];
      for (const auto &segment : record.control_segments) {
        const SegmentView view{index, &record, &segment};
        if (segment.kind == BookControlKind::table_start)
          table_open = true;
        if (segment.kind == BookControlKind::table_end)
          table_open = false;
        if (figure_start(segment)) {
          for (auto &enclosing : open)
            enclosing.segments.push_back(view);
          Region region;
          region.anchored = true;
          region.anchor = segment.opcode.substr(2);
          region.segments.push_back(view);
          if (table_open) {
            result.declined.push_back(
                decline(region, "figure starts inside a table", false));
            continue;
          }
          open.push_back(std::move(region));
          continue;
        }
        if (!open.empty()) {
          for (auto &enclosing : open)
            enclosing.segments.push_back(view);
          if (figure_end(record, segment)) {
            close(std::move(open.back()));
            open.pop_back();
          }
          continue;
        }
        if (segment.kind != BookControlKind::select)
          continue;
        const auto found = selectors_by_segment.find(
            {record.logical_record, segment.segment_index});
        if (found == selectors_by_segment.end())
          continue;
        for (const auto *selector : found->second) {
          if (!picture_selector(*selector))
            continue;
          Region region;
          region.anchored = false;
          region.segments.push_back(view);
          if (table_open || selector->inside_table) {
            result.declined.push_back(
                decline(region, "picture selector is table-owned"));
            continue;
          }
          if (inline_in_prose(region)) {
            result.declined.push_back(
                decline(region, figure_inline_picture_decline_reason()));
            continue;
          }
          close(std::move(region));
        }
      }
    }
    while (!open.empty()) {
      const auto &region = open.back();
      result.declined.push_back(decline(
          region,
          has_picture(region)
              ? "figure region is not terminated"
              : "figure region has no picture selector (unterminated)",
          false));
      open.pop_back();
    }
    return result;
  }
};

const char *role_name(FigureCellRoleIR role) {
  switch (role) {
  case FigureCellRoleIR::control: return "control";
  case FigureCellRoleIR::boundary: return "boundary";
  case FigureCellRoleIR::placeholder_suppressed: return "placeholder";
  case FigureCellRoleIR::caption_layout: return "caption-layout";
  case FigureCellRoleIR::caption_content: return "caption-content";
  case FigureCellRoleIR::index_term: return "index-term";
  case FigureCellRoleIR::description: return "description";
  case FigureCellRoleIR::line_prefix: return "line-prefix";
  case FigureCellRoleIR::body_content: return "body-content";
  case FigureCellRoleIR::body_layout: return "body-layout";
  case FigureCellRoleIR::spacing: return "spacing";
  }
  return "invalid";
}

} // namespace

const std::string &figure_inline_picture_decline_reason() {
  static const std::string reason = "picture selector is inline in prose";
  return reason;
}

bool figure_picture_target(const std::string &target) {
  if (target.size() <= 3 || !ascii_starts_with_case_insensitive(target, "pic"))
    return false;
  return std::all_of(target.begin() + 3, target.end(), [](const auto ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
  });
}

std::string figure_picture_resource(const std::string &target) {
  return figure_picture_target(target) ? target.substr(3) : std::string{};
}

std::string figure_picture_placeholder(const std::string &resource) {
  return "PICTURE " + resource;
}

std::string figure_display_glyph(std::uint16_t word) {
  if (word >= 0x20 && word <= 0x7E)
    return std::string(1, static_cast<char>(word));
  switch (word) {
  case 0xA0: return " ";
  // Hosted BookServer <pre> output of box-drawing words (FA1PLMM0
  // PREFACE.3, ACPZMST1 1.2.5, DREICMST 1.1.1.1, SC24-546 3.4).
  case 0x2500: return "_";
  case 0x2502: return "|";
  case 0x250C: case 0x2510: case 0x252C: return " ";
  case 0x2514: case 0x2518: case 0x2534:
  case 0x251C: case 0x2524: case 0x253C: return "|";
  // The decoder's bullet placeholder; hosted shows the degree-sign byte
  // (SH20-918 FRONT_1.3).
  case 0x2666: return "\xC2\xB0";
  // Arrows keep their glyph; hosted's per-book display tables turn them
  // into substitution or control bytes (ACPZMST1 1.2.5, SC34-425 1.3.4).
  case 0x2190: return "\xE2\x86\x90";
  case 0x2191: return "\xE2\x86\x91";
  case 0x2192: return "\xE2\x86\x92";
  case 0x2193: return "\xE2\x86\x93";
  default: break;
  }
  return token_words_to_ascii({word});
}

FigureBlocksIR extract_figure_blocks_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const SelectorCatalogIR &selectors,
    const std::set<std::string> &resource_ids) {
  std::set<std::string> lower_ids;
  for (const auto &id : resource_ids)
    lower_ids.insert(ascii_lower(id));
  return Extractor(records, layout, ownership, selectors, lower_ids).run();
}

bool verify_figure_blocks_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const SelectorCatalogIR &selectors,
    const std::set<std::string> &resource_ids, const FigureBlocksIR &blocks,
    std::string *error) {
  const auto fail = [&](const std::string &message) {
    if (error != nullptr)
      *error = message;
    return false;
  };
  std::map<CellKey, const OwnedSourceCellIR *> ledger;
  for (const auto &cell : ownership.cells)
    ledger[{cell.logical_record, cell.token_index, cell.word_index}] = &cell;
  std::set<CellKey> claimed;
  for (const auto &block : blocks.blocks) {
    const auto preformatted = block.body_kind == FigureBodyKindIR::preformatted;
    if (preformatted ? !block.target.empty() : block.target.empty())
      return fail("figure block target does not match its body kind");
    if (preformatted && (block.lines.empty() || !block.span.anchored))
      return fail("preformatted figure has no body lines");
    if (preformatted && (block.lines.front().text.empty() ||
                         block.lines.back().text.empty()))
      return fail("preformatted figure body is not trimmed");
    if (block.span.anchored == block.anchor.empty())
      return fail("figure anchor does not match its span kind");
    if (block.cells.empty())
      return fail("figure block owns no source cells");
    for (const auto &cell : block.cells) {
      const CellKey key{cell.logical_record, cell.token_index, cell.word_index};
      const auto owned = ledger.find(key);
      if (owned == ledger.end() || owned->second->word != cell.word)
        return fail("figure cell does not match the ownership ledger");
      if (!claimed.insert(key).second)
        return fail("figure cell is claimed more than once");
      const auto &owner = *owned->second;
      switch (cell.role) {
      case FigureCellRoleIR::caption_content:
        // Row-owned visible content, or segment-owned (row-less) text; the
        // content/layout split is by word, not by row disposition.  Inside a
        // drawn figure the Layout IR may have read a visible one-byte word
        // as a marker slot ("IMS", GG24-4302-00 record 262), so only control
        // material is rejected there.
        if (owner.disposition == SourceDisposition::control_operand ||
            (!preformatted &&
             owner.disposition == SourceDisposition::marker_slot))
          return fail("caption content cell is control or marker material");
        break;
      case FigureCellRoleIR::body_content:
      case FigureCellRoleIR::body_layout:
      case FigureCellRoleIR::line_prefix:
      case FigureCellRoleIR::spacing:
        // The ledger's control-operand reading of words inside drawn
        // lines is the same misread as above, so no disposition is
        // rejected; the canonical re-extraction below is the check.
        if (!preformatted)
          return fail("picture figure carries preformatted cell roles");
        break;
      case FigureCellRoleIR::caption_layout:
        if (owner.disposition == SourceDisposition::control_operand)
          return fail("caption layout cell is control material");
        break;
      case FigureCellRoleIR::control:
      case FigureCellRoleIR::boundary:
      case FigureCellRoleIR::placeholder_suppressed:
      case FigureCellRoleIR::index_term:
      // A description cell is the text of a `cartdesc` line, which the
      // ledger reads as that control's material; the canonical
      // re-extraction below checks it lands on the block's description.
      case FigureCellRoleIR::description:
        break;
      }
    }
    for (const auto &row : block.suppressed_rows)
      if (row.display_run == 0)
        return fail("suppressed row has no display run");
    if (block.caption && block.caption->text.empty())
      return fail("figure caption is incomplete");
  }
  const auto canonical = extract_figure_blocks_ir(records, layout, ownership,
                                                  selectors, resource_ids);
  if (canonical.blocks.size() != blocks.blocks.size() ||
      canonical.declined.size() != blocks.declined.size())
    return fail("figure blocks differ from the canonical extraction");
  for (std::size_t index = 0; index < blocks.blocks.size(); ++index)
    if (!same_block(canonical.blocks[index], blocks.blocks[index]))
      return fail("figure block differs from the canonical extraction");
  for (std::size_t index = 0; index < blocks.declined.size(); ++index)
    if (!same_decline(canonical.declined[index], blocks.declined[index]))
      return fail("figure decline differs from the canonical extraction");
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_figure_blocks_ir(const FigureBlocksIR &blocks) {
  std::ostringstream out;
  for (const auto &block : blocks.blocks) {
    out << "figure span=" << block.span.begin.logical_record << ':'
        << block.span.begin.segment_index << ".."
        << block.span.end.logical_record << ':' << block.span.end.segment_index
        << (block.span.anchored ? " anchor='" + block.anchor + "'"
                                : std::string{" anchorless"});
    if (block.body_kind == FigureBodyKindIR::preformatted)
      out << " preformatted lines=" << block.lines.size();
    else
      out << " target="
          << (block.target_kind == FigureTargetKindIR::book_resource
                  ? "resource:"
                  : "external:")
          << block.target << " selector=" << block.selector.logical_record
          << ':' << block.selector.segment_index << ':'
          << block.selector.ordinal;
    if (!block.placeholder_text.empty())
      out << " placeholder='" << block.placeholder_text << "'";
    if (block.caption)
      out << " caption='" << block.caption->text << "'";
    for (const auto &term : block.index_terms)
      out << " index='" << term << "'";
    out << " suppressed-rows=" << block.suppressed_rows.size()
        << " cells=" << block.cells.size();
    std::map<FigureCellRoleIR, std::size_t> roles;
    for (const auto &cell : block.cells)
      ++roles[cell.role];
    for (const auto &[role, count] : roles)
      out << ' ' << role_name(role) << '=' << count;
    out << '\n';
  }
  for (const auto &declined : blocks.declined) {
    out << "declined begin=" << declined.begin.logical_record << ':'
        << declined.begin.segment_index;
    if (declined.end)
      out << " end=" << declined.end->logical_record << ':'
          << declined.end->segment_index;
    if (!declined.anchor.empty())
      out << " anchor='" << declined.anchor << "'";
    out << " reason='" << declined.reason << "'\n";
  }
  return out.str();
}

} // namespace geist::detail
