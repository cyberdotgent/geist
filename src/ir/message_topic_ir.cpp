// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/ir/message_topic_ir.hpp"

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/selector_display_ir.hpp"
#include "geist/detail/ir/selector_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace geist::detail {
namespace {

using SegmentKey = std::pair<std::uint32_t, std::size_t>;
using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

std::string range_text(const DecodedLogicalRecordSource &record,
                       const OutputRangeIR &range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size())
    return {};
  return text.substr(range.begin, range.end - range.begin);
}

DocumentSourceSliceIR source_slice(const DecodedLogicalRecordSource &record,
                                   std::size_t segment_index,
                                   const OutputRangeIR &output) {
  DocumentSourceSliceIR result;
  result.logical_record = record.logical_record;
  result.segment_index = segment_index;
  const auto words = decoded_byte_range_to_word_range(record.assembled, output);
  const auto tokens = source_tokens_intersecting_output(record.assembled,
                                                        words.begin, words.end);
  if (tokens.empty())
    return result;
  result.token_begin = tokens.front();
  result.token_end = tokens.back() + 1;
  result.byte_begin = record.ir.tokens[result.token_begin].byte_range.begin;
  result.byte_end = record.ir.tokens[result.token_end - 1].byte_range.end;
  return result;
}

DocumentSourceSliceIR row_slice(const DecodedLogicalRecordSource &record,
                                const PhysicalRowIR &row) {
  DocumentSourceSliceIR result;
  result.logical_record = row.logical_record;
  result.segment_index = row.segment_index;
  result.token_begin = row.token_begin;
  result.token_end = row.token_end;
  if (row.token_begin < row.token_end &&
      row.token_end <= record.ir.tokens.size()) {
    result.byte_begin = record.ir.tokens[row.token_begin].byte_range.begin;
    result.byte_end = record.ir.tokens[row.token_end - 1].byte_range.end;
  }
  return result;
}

bool has_source(const DocumentSourceSliceIR &source) {
  return source.logical_record != 0 && source.token_begin < source.token_end &&
         source.byte_begin < source.byte_end;
}

const DecodedLogicalRecordSource *
find_record(const std::vector<DecodedLogicalRecordSource> &records,
            std::uint32_t logical_record) {
  const auto found =
      std::find_if(records.begin(), records.end(), [&](const auto &record) {
        return record.logical_record == logical_record;
      });
  return found == records.end() ? nullptr : &*found;
}

std::string operand(const DecodedLogicalRecordSource &record,
                    const ControlSegmentIR &segment) {
  auto value = collapse_ascii_whitespace(
      trim_ascii(range_text(record, segment.operand_range)));
  if (!value.empty() && value.front() == ':')
    value.erase(value.begin());
  return value;
}

std::string topic_id(const ControlSegmentIR &segment) {
  if (segment.opcode.size() <= 2 ||
      !ascii_equals_case_insensitive(segment.opcode.substr(0, 2), "sh"))
    return {};
  return segment.opcode.substr(2);
}

bool numeric_id_part(const std::string &value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

bool numeric_message_id(const std::string &value) {
  if (numeric_id_part(value))
    return true;
  const auto hyphen = value.find('-');
  return hyphen != std::string::npos &&
         value.find('-', hyphen + 1) == std::string::npos &&
         numeric_id_part(value.substr(0, hyphen)) &&
         numeric_id_part(value.substr(hyphen + 1));
}

bool closes_unmatched_delimiter(std::string_view prefix,
                                std::string_view closing) {
  if (closing.size() != 1)
    return false;
  char opener = '\0';
  switch (closing.front()) {
  case '>': opener = '<'; break;
  case ')': opener = '('; break;
  case ']': opener = '['; break;
  case '}': opener = '{'; break;
  default: return false;
  }
  std::size_t depth = 0;
  for (const auto character : prefix) {
    if (character == opener)
      ++depth;
    else if (character == closing.front() && depth != 0)
      --depth;
  }
  return depth != 0;
}

std::string first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n");
  return value.substr(0, end);
}

bool same_slice(const DocumentSourceSliceIR &left,
                const DocumentSourceSliceIR &right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.token_begin == right.token_begin &&
         left.token_end == right.token_end &&
         left.byte_begin == right.byte_begin && left.byte_end == right.byte_end;
}

bool same_introduction(const MessageIntroductionIR &left,
                       const MessageIntroductionIR &right) {
  if (left.cells.size() != right.cells.size() ||
      left.paragraphs.size() != right.paragraphs.size())
    return false;
  for (std::size_t index = 0; index < left.cells.size(); ++index) {
    const auto &a = left.cells[index];
    const auto &b = right.cells[index];
    if (a.logical_record != b.logical_record ||
        a.token_index != b.token_index || a.word_index != b.word_index ||
        a.word != b.word || a.source_disposition != b.source_disposition ||
        a.role != b.role || a.introduction_row != b.introduction_row)
      return false;
  }
  for (std::size_t paragraph = 0; paragraph < left.paragraphs.size();
       ++paragraph) {
    const auto &a = left.paragraphs[paragraph];
    const auto &b = right.paragraphs[paragraph];
    if (a.atoms.size() != b.atoms.size())
      return false;
    for (std::size_t atom = 0; atom < a.atoms.size(); ++atom) {
      const auto &x = a.atoms[atom];
      const auto &y = b.atoms[atom];
      if (x.kind != y.kind || x.text != y.text ||
          x.target.has_value() != y.target.has_value() ||
          x.cell_indices != y.cell_indices)
        return false;
      if (x.target && (x.target->kind != y.target->kind ||
                       x.target->value != y.target->value))
        return false;
    }
  }
  return true;
}

std::string display_text(const std::vector<SelectorDisplayCellIR> &cells,
                         std::size_t begin, std::size_t end) {
  std::string result;
  for (auto index = begin; index < end && index < cells.size(); ++index)
    if (cells[index].word <= 0xff)
      result.push_back(static_cast<char>(cells[index].word));
  return collapse_ascii_whitespace(trim_ascii(std::move(result)));
}

bool closes_left(std::uint16_t word) {
  return word == '.' || word == ',' || word == ':' || word == ';' ||
         word == '!' || word == '?' || word == ')' || word == ']' ||
         word == '}' || word == '/';
}

bool opens_right(std::uint16_t word) {
  return word == '(' || word == '[' || word == '{' || word == '/' ||
         word == '<';
}

std::string
compose_source_cells(const std::vector<MessageIntroductionCellIR> &cells,
                     const std::vector<std::size_t> &indices) {
  struct TokenText {
    std::size_t cell_begin = 0;
    std::size_t cell_end = 0;
    std::string text;
  };
  std::vector<TokenText> tokens;
  for (const auto index : indices) {
    if (index >= cells.size() || cells[index].word > 0xff)
      continue;
    const auto &cell = cells[index];
    if (tokens.empty() ||
        cells[tokens.back().cell_end].logical_record != cell.logical_record ||
        cells[tokens.back().cell_end].token_index != cell.token_index) {
      tokens.push_back({index, index, {}});
    } else {
      tokens.back().cell_end = index;
    }
    tokens.back().text.push_back(static_cast<char>(cell.word));
  }

  std::string result;
  auto open_quote = false;
  std::uint16_t previous_last = 0;
  for (const auto &token : tokens) {
    if (token.text.empty())
      continue;
    const auto first = static_cast<unsigned char>(token.text.front());
    const auto quote = token.text == "\"";
    const auto tight_left =
        closes_left(first) || opens_right(previous_last) || open_quote;
    if (!result.empty() && !tight_left)
      result.push_back(' ');
    result += token.text;
    if (quote)
      open_quote = !open_quote;
    previous_last = static_cast<unsigned char>(token.text.back());
  }
  return result;
}

std::optional<MessageIntroductionIR> extract_message_introduction_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const VerifiedOwnershipIR &verified_ownership,
    const SelectorCatalogIR &selectors, const MessageTopicIR &topic,
    std::string *error) {
  const auto reject =
      [&](std::string message) -> std::optional<MessageIntroductionIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (topic.introduction_row_indices.size() != 20)
    return reject("message introduction row geometry is not canonical");

  MessageIntroductionIR result;
  std::map<CellKey, std::size_t> cell_by_source;
  std::vector<std::vector<std::size_t>> row_cells;
  row_cells.resize(topic.introduction_row_indices.size());
  for (std::size_t local_row = 0;
       local_row < topic.introduction_row_indices.size(); ++local_row) {
    const auto global_row = topic.introduction_row_indices[local_row];
    if (global_row >= topic.rows.size())
      return reject("message introduction row index is outside its ledger");
    const auto &row = topic.rows[global_row];
    for (const auto &source : row.cells) {
      MessageIntroductionCellIR cell;
      cell.logical_record = source.logical_record;
      cell.token_index = source.token_index;
      cell.word_index = source.word_index;
      cell.word = source.word;
      cell.source_disposition = source.disposition;
      cell.introduction_row = local_row;
      if (source.disposition == SourceDisposition::visible_content)
        cell.role = MessageIntroductionCellRoleIR::text;
      const auto index = result.cells.size();
      if (!cell_by_source
               .emplace(CellKey{cell.logical_record, cell.token_index,
                                cell.word_index},
                        index)
               .second)
        return reject("message introduction duplicates a source cell");
      result.cells.push_back(cell);
      row_cells[local_row].push_back(index);
    }

    // A compact marker followed by a non-native origin is a lexical carry,
    // not a layout marker. This classifies punctuation and word carry without
    // matching either decoded spelling.
    if (row.marker && row.native_origin != 3) {
      for (const auto cell_index : row_cells[local_row])
        if (result.cells[cell_index].token_index == row.marker->token_index)
          result.cells[cell_index].role = MessageIntroductionCellRoleIR::text;
    }
  }

  // Find the one paragraph boundary encoded inside a physical row: terminal
  // punctuation followed by a wide, source-owned padding run and more text.
  std::optional<std::pair<std::size_t, std::size_t>> inline_break;
  for (std::size_t local_row = 0; local_row < row_cells.size(); ++local_row) {
    const auto &indices = row_cells[local_row];
    for (std::size_t at = 0; at < indices.size();) {
      if (result.cells[indices[at]].source_disposition !=
          SourceDisposition::layout_padding) {
        ++at;
        continue;
      }
      const auto begin = at;
      while (at < indices.size() &&
             result.cells[indices[at]].source_disposition ==
                 SourceDisposition::layout_padding)
        ++at;
      const auto width = at - begin;
      auto previous = begin;
      while (previous != 0) {
        --previous;
        if (result.cells[indices[previous]].role ==
            MessageIntroductionCellRoleIR::text)
          break;
      }
      auto next = at;
      while (next < indices.size() && result.cells[indices[next]].role !=
                                          MessageIntroductionCellRoleIR::text)
        ++next;
      if (width >= 5 && previous < begin && next < indices.size() &&
          result.cells[indices[previous]].word == '.') {
        if (inline_break)
          return reject(
              "message introduction has multiple inline paragraph gaps");
        inline_break = {local_row, indices[next]};
        for (auto pad = begin; pad < at; ++pad)
          result.cells[indices[pad]].role =
              MessageIntroductionCellRoleIR::paragraph_break;
      }
    }
  }
  if (!inline_break)
    return reject("message introduction lacks its inline paragraph gap");

  std::vector<std::vector<std::size_t>> paragraph_cells(1);
  const auto start_paragraph = [&]() { paragraph_cells.emplace_back(); };
  for (std::size_t local_row = 0; local_row < row_cells.size(); ++local_row) {
    const auto &row = topic.rows[topic.introduction_row_indices[local_row]];
    const auto lexical_marker = row.marker && row.native_origin != 3;
    const auto previous_text =
        std::find_if(paragraph_cells.back().rbegin(),
                     paragraph_cells.back().rend(), [&](const auto index) {
                       return result.cells[index].role ==
                              MessageIntroductionCellRoleIR::text;
                     });
    if (local_row != 0 && !lexical_marker && row.native_origin == 3 &&
        previous_text != paragraph_cells.back().rend() &&
        result.cells[*previous_text].word == '.')
      start_paragraph();

    for (const auto cell_index : row_cells[local_row]) {
      if (cell_index == inline_break->second)
        start_paragraph();
      if (result.cells[cell_index].role == MessageIntroductionCellRoleIR::text)
        paragraph_cells.back().push_back(cell_index);
    }
  }
  if (paragraph_cells.size() != 5 ||
      std::any_of(paragraph_cells.begin(), paragraph_cells.end(),
                  [](const auto &cells) { return cells.empty(); }))
    return reject("message introduction paragraph partition is not canonical");

  std::string display_error;
  const auto display = extract_selector_display_ir(
      records, selectors, layout, verified_ownership, &display_error);
  if (!display ||
      !verify_selector_display_ir(records, selectors, layout,
                                  verified_ownership, *display,
                                  &display_error) ||
      display->rows.size() != 2 || display->bindings.size() != 2 ||
      display->rows[0].spans.size() != 1 || display->rows[1].spans.size() != 1)
    return reject("message introduction selector display rejected: " +
                  display_error);

  struct LinkProjection {
    std::string label;
    CrossReferenceTargetIR target;
    std::vector<std::size_t> cells;
  };
  std::vector<LinkProjection> links;
  for (const auto &display_row : display->rows) {
    const auto &span = display_row.spans.front();
    LinkProjection link;
    link.label =
        display_text(display_row.cells, span.cell_begin, span.cell_end);
    link.target = {CrossReferenceTargetKindIR::anchor, span.target.raw_target};
    for (auto cell = span.cell_begin; cell < span.cell_end; ++cell) {
      if (cell >= display_row.cells.size() || !display_row.cells[cell].source ||
          display_row.cells[cell].source->kind !=
              SelectorSourceCellKind::token_word)
        continue;
      const auto &source = *display_row.cells[cell].source;
      const auto found = cell_by_source.find(
          {source.logical_record, source.token_index, source.word_index});
      if (found != cell_by_source.end()) {
        result.cells[found->second].role =
            MessageIntroductionCellRoleIR::selector;
        if (link.cells.empty() || link.cells.back() != found->second)
          link.cells.push_back(found->second);
      }
    }
    if (link.label.empty() || link.cells.empty())
      return reject("message introduction selector lacks source cells");
    links.push_back(std::move(link));
  }
  for (std::size_t paragraph = 0; paragraph < 4; ++paragraph) {
    MessageIntroductionAtomIR atom;
    atom.cell_indices = paragraph_cells[paragraph];
    atom.text = compose_source_cells(result.cells, atom.cell_indices);
    if (atom.text.empty())
      return reject("message introduction paragraph has no source text");
    result.paragraphs.push_back({{std::move(atom)}});
  }

  const auto first_source = links[0].cells.front();
  const auto first_end = links[0].cells.back();
  const auto second_source = links[1].cells.front();
  const auto second_end = links[1].cells.back();
  if (!std::is_sorted(links[0].cells.begin(), links[0].cells.end()) ||
      !std::is_sorted(links[1].cells.begin(), links[1].cells.end()) ||
      first_source > first_end || second_source > second_end ||
      first_end >= second_source)
    return reject("message introduction selector source order is invalid");
  MessageIntroductionParagraphIR last;
  const auto add_text_atom = [&](std::size_t begin, std::size_t end,
                                 bool leading_space, bool trailing_space) {
    MessageIntroductionAtomIR atom;
    for (const auto cell : paragraph_cells.back())
      if (cell >= begin && cell < end &&
          result.cells[cell].role == MessageIntroductionCellRoleIR::text)
        atom.cell_indices.push_back(cell);
    atom.text = compose_source_cells(result.cells, atom.cell_indices);
    if (leading_space)
      atom.text.insert(atom.text.begin(), ' ');
    if (trailing_space)
      atom.text.push_back(' ');
    if (atom.text.empty())
      return;
    last.atoms.push_back(std::move(atom));
  };
  add_text_atom(0, first_source, false, true);
  last.atoms.push_back({MessageIntroductionAtomKindIR::selector, links[0].label,
                        links[0].target, links[0].cells});
  add_text_atom(first_end + 1, second_source, true, false);
  last.atoms.push_back({MessageIntroductionAtomKindIR::selector, links[1].label,
                        links[1].target, links[1].cells});
  add_text_atom(second_end + 1, result.cells.size(), true, false);
  if (last.atoms.size() != 5)
    return reject("message introduction inline sequence is not canonical");
  result.paragraphs.push_back(std::move(last));

  std::vector<std::size_t> claims(result.cells.size());
  for (const auto &paragraph : result.paragraphs)
    for (const auto &atom : paragraph.atoms)
      for (const auto cell : atom.cell_indices) {
        if (cell >= result.cells.size())
          return reject("message introduction atom has an invalid source cell");
        ++claims[cell];
      }
  for (std::size_t cell = 0; cell < result.cells.size(); ++cell) {
    const auto semantic =
        result.cells[cell].role == MessageIntroductionCellRoleIR::text ||
        result.cells[cell].role == MessageIntroductionCellRoleIR::selector;
    if ((semantic && claims[cell] != 1) || (!semantic && claims[cell] != 0))
      return reject("message introduction source-cell claims are not exact");
  }
  if (error != nullptr)
    error->clear();
  return result;
}

bool same_topic_envelope(const MessageTopicIR &left,
                         const MessageTopicIR &right) {
  if (left.first_logical_record != right.first_logical_record ||
      left.end_logical_record != right.end_logical_record ||
      left.metadata.raw_topic_id != right.metadata.raw_topic_id ||
      left.metadata.topic_number != right.metadata.topic_number ||
      left.metadata.parent != right.metadata.parent ||
      left.metadata.forward_level != right.metadata.forward_level ||
      left.metadata.back_level != right.metadata.back_level ||
      left.metadata.summary != right.metadata.summary ||
      left.metadata.heading_level != right.metadata.heading_level ||
      left.metadata.source_file != right.metadata.source_file ||
      left.title != right.title ||
      left.heading_row_indices != right.heading_row_indices ||
      left.introduction_row_indices != right.introduction_row_indices ||
      left.anchors.size() != right.anchors.size() ||
      left.selectors.size() != right.selectors.size() ||
      !same_introduction(left.introduction, right.introduction) ||
      left.rows.size() != right.rows.size() ||
      left.segments.size() != right.segments.size() ||
      left.source_tokens.size() != right.source_tokens.size() ||
      !same_slice(left.terminal_content_source, right.terminal_content_source))
    return false;
  for (std::size_t index = 0; index < left.anchors.size(); ++index)
    if (left.anchors[index].id != right.anchors[index].id ||
        !same_slice(left.anchors[index].source, right.anchors[index].source))
      return false;
  for (std::size_t index = 0; index < left.selectors.size(); ++index) {
    const auto &a = left.selectors[index];
    const auto &b = right.selectors[index];
    if (a.target.kind != b.target.kind || a.target.value != b.target.value ||
        a.display_payload != b.display_payload || a.column != b.column ||
        a.length != b.length || !same_slice(a.source, b.source))
      return false;
  }
  for (std::size_t index = 0; index < left.rows.size(); ++index) {
    const auto &a = left.rows[index];
    const auto &b = right.rows[index];
    if (a.visible_text != b.visible_text ||
        a.marker.has_value() != b.marker.has_value() ||
        a.native_origin != b.native_origin ||
        a.break_before != b.break_before ||
        a.source_row.display_run != b.source_row.display_run ||
        a.source_row.row_index != b.source_row.row_index ||
        !same_slice(a.source, b.source) || a.cells.size() != b.cells.size())
      return false;
    if (a.marker && (a.marker->logical_record != b.marker->logical_record ||
                     a.marker->token_index != b.marker->token_index ||
                     a.marker->encoded_value != b.marker->encoded_value ||
                     a.marker->encoded_width != b.marker->encoded_width ||
                     a.marker->byte_range.begin != b.marker->byte_range.begin ||
                     a.marker->byte_range.end != b.marker->byte_range.end ||
                     a.marker->decoded_text != b.marker->decoded_text))
      return false;
    for (std::size_t cell = 0; cell < a.cells.size(); ++cell) {
      const auto &x = a.cells[cell];
      const auto &y = b.cells[cell];
      if (x.logical_record != y.logical_record ||
          x.token_index != y.token_index || x.word_index != y.word_index ||
          x.word != y.word || x.disposition != y.disposition)
        return false;
    }
  }
  for (std::size_t index = 0; index < left.segments.size(); ++index) {
    const auto &a = left.segments[index];
    const auto &b = right.segments[index];
    if (a.kind != b.kind || a.opcode != b.opcode ||
        a.malformed != b.malformed || a.role != b.role ||
        !same_slice(a.source, b.source))
      return false;
  }
  for (std::size_t index = 0; index < left.source_tokens.size(); ++index) {
    const auto &a = left.source_tokens[index];
    const auto &b = right.source_tokens[index];
    if (a.logical_record != b.logical_record ||
        a.token_index != b.token_index || !(a.encoded == b.encoded) ||
        a.bytes.begin != b.bytes.begin || a.bytes.end != b.bytes.end ||
        a.decoded_segment != b.decoded_segment)
      return false;
  }
  return true;
}

} // namespace

std::optional<MessageTopicIR> extract_message_topic_ir_impl(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const VerifiedOwnershipIR &verified_ownership,
    bool include_catalog, std::string *error) {
  const auto reject =
      [&](std::string message) -> std::optional<MessageTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty())
    return reject("message topic has no logical records");
  const OwnershipIR &ownership = verified_ownership;
  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !ownership_verified_for(verified_ownership, records, layout,
                              &verification_error))
    return reject("source layout/ownership is not canonical: " +
                  verification_error);
  for (std::size_t index = 1; index < records.size(); ++index)
    if (records[index].logical_record != records[index - 1].logical_record + 1)
      return reject("message logical-record envelope is not contiguous");

  struct OrderedSegment {
    const DecodedLogicalRecordSource *record = nullptr;
    const ControlSegmentIR *segment = nullptr;
  };
  std::vector<OrderedSegment> ordered;
  for (const auto &record : records)
    for (const auto &segment : record.control_segments)
      ordered.push_back({&record, &segment});
  if (ordered.empty())
    return reject("message topic has no control segments");

  // A token can belong to at most one decoded segment. Separator tokens which
  // fall between segments are retained explicitly in the token ledger.
  std::vector<MessageTopicSourceTokenIR> source_token_ledger;
  for (const auto &record : records) {
    std::vector<std::size_t> token_claims(record.ir.tokens.size());
    std::vector<std::optional<std::size_t>> segment_by_token(
        record.ir.tokens.size());
    for (const auto &segment : record.control_segments) {
      for (const auto token : segment.source_tokens) {
        if (token >= token_claims.size())
          return reject("message segment claims an out-of-range source token");
        ++token_claims[token];
        segment_by_token[token] = segment.segment_index;
      }
    }
    if (std::any_of(token_claims.begin(), token_claims.end(),
                    [](const auto claims) { return claims > 1; }))
      return reject("message decoded segments duplicate a source-token claim");
    for (std::size_t token = 0; token < record.ir.tokens.size(); ++token) {
      const auto &source = record.ir.tokens[token];
      source_token_ledger.push_back({record.logical_record, token,
                                     source.encoded, source.byte_range,
                                     segment_by_token[token]});
    }
  }

  const auto first_numeric =
      std::find_if(ordered.begin(), ordered.end(), [](const auto &item) {
        return item.segment->kind == BookControlKind::message_start &&
               numeric_message_id(first_word(
                   range_text(*item.record, item.segment->operand_range)));
      });
  if (first_numeric == ordered.end())
    return reject("message topic has no numeric SRMSG catalog boundary");
  const SegmentKey first_catalog_key{first_numeric->record->logical_record,
                                     first_numeric->segment->segment_index};

  MessageTopicIR result;
  result.first_logical_record = records.front().logical_record;
  result.end_logical_record = records.back().logical_record + 1;
  result.source_tokens = std::move(source_token_ledger);
  std::map<BookControlKind, std::size_t> metadata_counts;
  std::optional<SegmentKey> title_key;
  for (auto it = ordered.begin(); it != first_numeric; ++it) {
    const auto &segment = *it->segment;
    const auto value = operand(*it->record, segment);
    switch (segment.kind) {
    case BookControlKind::topic_start:
      result.metadata.raw_topic_id = topic_id(segment);
      ++metadata_counts[segment.kind];
      break;
    case BookControlKind::topic_number:
      result.metadata.topic_number = value;
      ++metadata_counts[segment.kind];
      break;
    case BookControlKind::parent:
      result.metadata.parent = value;
      ++metadata_counts[segment.kind];
      break;
    case BookControlKind::forward_level:
      result.metadata.forward_level = value;
      ++metadata_counts[segment.kind];
      break;
    case BookControlKind::back_level:
      result.metadata.back_level = value;
      ++metadata_counts[segment.kind];
      break;
    case BookControlKind::summary:
      result.metadata.summary = value;
      ++metadata_counts[segment.kind];
      break;
    case BookControlKind::heading_level:
      result.metadata.heading_level = value;
      ++metadata_counts[segment.kind];
      break;
    case BookControlKind::source_file:
      result.metadata.source_file = value;
      ++metadata_counts[segment.kind];
      break;
    case BookControlKind::title:
      if (title_key)
        return reject("message topic has multiple title segments");
      title_key = {it->record->logical_record, segment.segment_index};
      break;
    default:
      break;
    }
  }
  const std::vector<BookControlKind> required = {
      BookControlKind::topic_start,   BookControlKind::topic_number,
      BookControlKind::parent,        BookControlKind::forward_level,
      BookControlKind::back_level,    BookControlKind::summary,
      BookControlKind::heading_level, BookControlKind::source_file};
  if (!title_key ||
      std::any_of(
          required.begin(), required.end(),
          [&](const auto kind) { return metadata_counts[kind] != 1; }) ||
      result.metadata.raw_topic_id != "5.0" ||
      !ascii_equals_case_insensitive(result.metadata.heading_level, "H1"))
    return reject("message topic metadata/heading envelope is incomplete");

  if (include_catalog) {
    auto catalog = extract_message_catalog_ir(records, layout, ownership,
                                              &verification_error);
    if (!catalog)
      return reject("inner message catalog rejected: " + verification_error);
    if (catalog->entries.size() != 396 ||
        catalog->entries.front().id != "023" ||
        catalog->entries.back().id != "2505")
      return reject("message catalog fixture boundary is not canonical");
    result.catalog = std::move(*catalog);
  }

  const auto selectors =
      extract_selector_catalog_ir(records, &verification_error);
  if (!selectors)
    return reject("message selectors rejected: " + verification_error);
  if (selectors->selectors.size() != 2)
    return reject("message introduction selector count is not canonical");
  for (const auto &selector : selectors->selectors) {
    if (!selector.canonical_operands || selector.inside_table ||
        selector.target != "HDRPROBS")
      return reject("message introduction selector is not canonical");
    const auto *record = find_record(records, selector.logical_record);
    if (record == nullptr)
      return reject("message selector refers to an absent record");
    auto source =
        source_slice(*record, selector.segment_index, selector.complete_range);
    if (!has_source(source))
      return reject("message selector provenance is incomplete");
    result.selectors.push_back(
        {{CrossReferenceTargetKindIR::anchor, selector.target},
         selector.display_payload,
         selector.column,
         selector.length,
         std::move(source)});
  }

  for (const auto &run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      const auto *record = find_record(records, row.logical_record);
      if (record == nullptr)
        return reject("message row refers to an absent record");
      MessageTopicRowIR item;
      item.visible_text = row.visible_text;
      item.marker = row.marker;
      item.native_origin = row.native_origin;
      item.break_before = row.break_before;
      item.source_row = {run.id, row_index};
      item.source = row_slice(*record, row);
      if (!has_source(item.source))
        return reject("message row provenance is incomplete");
      for (const auto &cell : ownership.cells)
        if (cell.run == run.id && cell.row_index == row_index)
          item.cells.push_back({cell.logical_record, cell.token_index,
                                cell.word_index, cell.word, cell.disposition});
      if (item.cells.empty())
        return reject("message row has no owned source cells");
      const auto global_index = result.rows.size();
      result.rows.push_back(std::move(item));
      const SegmentKey key{row.logical_record, row.segment_index};
      if (key == *title_key) {
        result.heading_row_indices.push_back(global_index);
      } else if (key < first_catalog_key &&
                 (run.control_kind == BookControlKind::text ||
                  run.control_kind == BookControlKind::font ||
                  run.control_kind == BookControlKind::select)) {
        result.introduction_row_indices.push_back(global_index);
      }
    }
  }
  if (result.heading_row_indices.size() != 2 ||
      result.introduction_row_indices.empty())
    return reject("message heading/introduction rows are incomplete");
  for (const auto index : result.heading_row_indices) {
    const auto &row = result.rows[index];
    if (!result.title.empty() && row.marker &&
        row.marker->decoded_text.size() == 1 &&
        std::string(".,:;!? ").find(row.marker->decoded_text.front()) !=
            std::string::npos)
      result.title += row.marker->decoded_text;
    if (!result.title.empty())
      result.title += ' ';
    result.title += collapse_ascii_whitespace(trim_ascii(row.visible_text));
  }
  if (result.title != "Chapter 5. Messages")
    return reject("message topic title projection is not canonical");

  auto introduction = extract_message_introduction_ir(
      records, layout, verified_ownership, *selectors, result,
      &verification_error);
  if (!introduction)
    return reject("message introduction rejected: " + verification_error);
  result.introduction = std::move(*introduction);

  std::set<SegmentKey> ledger_keys;
  for (auto it = ordered.begin(); it != ordered.end(); ++it) {
    const auto &segment = *it->segment;
    const SegmentKey key{it->record->logical_record, segment.segment_index};
    if (!ledger_keys.insert(key).second)
      return reject("message segment ledger contains a duplicate key");
    auto source =
        source_slice(*it->record, segment.segment_index, segment.complete);
    if (!has_source(source))
      return reject("message segment provenance is incomplete");
    MessageTopicSegmentRoleIR role = MessageTopicSegmentRoleIR::catalog;
    if (it < first_numeric) {
      if (segment.kind == BookControlKind::topic_start ||
          segment.kind == BookControlKind::topic_number ||
          segment.kind == BookControlKind::parent ||
          segment.kind == BookControlKind::forward_level ||
          segment.kind == BookControlKind::back_level ||
          segment.kind == BookControlKind::summary ||
          segment.kind == BookControlKind::heading_level ||
          segment.kind == BookControlKind::source_file)
        role = MessageTopicSegmentRoleIR::metadata;
      else if (segment.kind == BookControlKind::title)
        role = MessageTopicSegmentRoleIR::heading;
      else if (segment.kind == BookControlKind::select)
        role = MessageTopicSegmentRoleIR::selector;
      else if (segment.kind == BookControlKind::message_start ||
               ascii_equals_case_insensitive(segment.opcode, "SRHDRMSGS"))
        role = MessageTopicSegmentRoleIR::anchor;
      else
        role = MessageTopicSegmentRoleIR::introduction;
    }
    result.segments.push_back({segment.kind, segment.opcode, segment.malformed,
                               role, std::move(source)});

    if (segment.kind == BookControlKind::message_start) {
      const auto id =
          first_word(range_text(*it->record, segment.operand_range));
      if (numeric_message_id(id))
        result.anchors.push_back({"MSG " + id, result.segments.back().source});
      else if (it < first_numeric && id.empty())
        result.anchors.push_back({"MSG", result.segments.back().source});
    } else if (it < first_numeric &&
               ascii_equals_case_insensitive(segment.opcode, "SRHDRMSGS")) {
      result.anchors.push_back({"HDRMSGS", result.segments.back().source});
    }
  }
  const auto numeric_messages = static_cast<std::size_t>(std::count_if(
      ordered.begin(), ordered.end(), [](const auto &item) {
        return item.segment->kind == BookControlKind::message_start &&
               numeric_message_id(first_word(
                   range_text(*item.record, item.segment->operand_range)));
      }));
  if (result.segments.size() != ordered.size() ||
      result.anchors.size() != numeric_messages + 2)
    return reject("message source ledger or anchor set is incomplete");
  result.terminal_content_source = result.segments.back().source;
  if (result.segments.back().role != MessageTopicSegmentRoleIR::catalog ||
      result.segments.back().kind != BookControlKind::font)
    return reject("message topic terminal Action content is not conserved");

  if (error != nullptr)
    error->clear();
  return result;
}

std::optional<MessageTopicIR>
extract_message_topic_ir(const std::vector<DecodedLogicalRecordSource> &records,
                         const LayoutIR &layout, const VerifiedOwnershipIR &ownership,
                         std::string *error) {
  return extract_message_topic_ir_impl(records, layout, ownership, true, error);
}

bool message_topic_candidate_is_consistent(const MessageTopicIR &topic) {
  const auto &introduction = topic.introduction;
  if (topic.first_logical_record >= topic.end_logical_record ||
      introduction.cells.empty() || introduction.paragraphs.size() != 5)
    return false;
  std::set<std::size_t> paragraph_break_cells;
  for (std::size_t begin = 0; begin < introduction.cells.size();) {
    if (introduction.cells[begin].source_disposition !=
        SourceDisposition::layout_padding) {
      ++begin;
      continue;
    }
    auto end = begin + 1;
    while (end < introduction.cells.size() &&
           introduction.cells[end].introduction_row ==
               introduction.cells[begin].introduction_row &&
           introduction.cells[end].source_disposition ==
               SourceDisposition::layout_padding)
      ++end;
    auto previous = begin;
    while (previous != 0 &&
           introduction.cells[previous - 1].introduction_row ==
               introduction.cells[begin].introduction_row) {
      --previous;
      if (introduction.cells[previous].role ==
          MessageIntroductionCellRoleIR::text)
        break;
    }
    auto next = end;
    while (next < introduction.cells.size() &&
           introduction.cells[next].introduction_row ==
               introduction.cells[begin].introduction_row &&
           introduction.cells[next].role !=
               MessageIntroductionCellRoleIR::text)
      ++next;
    if (end - begin >= 5 && previous < begin &&
        introduction.cells[previous].word == '.' &&
        next < introduction.cells.size() &&
        introduction.cells[next].introduction_row ==
            introduction.cells[begin].introduction_row)
      for (auto cell = begin; cell < end; ++cell)
        paragraph_break_cells.insert(cell);
    begin = end;
  }
  if (paragraph_break_cells.empty())
    return false;
  std::vector<std::size_t> claims(introduction.cells.size());
  std::size_t selector = 0;
  std::optional<std::size_t> previous_source;
  for (const auto &paragraph : introduction.paragraphs) {
    if (paragraph.atoms.empty())
      return false;
    for (const auto &atom : paragraph.atoms) {
      if (atom.cell_indices.empty() ||
          !std::is_sorted(atom.cell_indices.begin(), atom.cell_indices.end()))
        return false;
      if (previous_source && atom.cell_indices.front() <= *previous_source)
        return false;
      previous_source = atom.cell_indices.back();
      for (const auto cell : atom.cell_indices) {
        if (cell >= introduction.cells.size())
          return false;
        ++claims[cell];
      }
      if (atom.kind == MessageIntroductionAtomKindIR::selector) {
        if (!atom.target || selector >= topic.selectors.size() ||
            atom.target->kind != topic.selectors[selector].target.kind ||
            atom.target->value != topic.selectors[selector].target.value)
          return false;
        ++selector;
      } else if (atom.target) {
        return false;
      }
    }
  }
  if (selector != topic.selectors.size())
    return false;
  for (std::size_t cell = 0; cell < introduction.cells.size(); ++cell) {
    const auto role = introduction.cells[cell].role;
    const auto semantic = role == MessageIntroductionCellRoleIR::text ||
                          role == MessageIntroductionCellRoleIR::selector;
    if ((semantic && claims[cell] != 1) || (!semantic && claims[cell] != 0) ||
        (role == MessageIntroductionCellRoleIR::paragraph_break) !=
            (paragraph_break_cells.count(cell) != 0))
      return false;
  }
  const auto coordinate = [](const auto &source) {
    return std::make_tuple(source.logical_record, source.segment_index,
                           source.token_begin, source.byte_begin);
  };
  for (std::size_t index = 1; index < topic.anchors.size(); ++index)
    if (coordinate(topic.anchors[index].source) <=
        coordinate(topic.anchors[index - 1].source))
      return false;
  return true;
}

bool message_catalog_candidate_is_consistent(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const MessageCatalogIR &catalog) {
  const auto valid_slice = [&](const DocumentSourceSliceIR &slice) {
    const auto *record = find_record(records, slice.logical_record);
    return record != nullptr && slice.token_begin < slice.token_end &&
           slice.token_end <= record->ir.tokens.size() &&
           slice.byte_begin ==
               record->ir.tokens[slice.token_begin].byte_range.begin &&
           slice.byte_end ==
               record->ir.tokens[slice.token_end - 1].byte_range.end;
  };
  const auto valid_terminal_token = [&](const MessageTerminalLayoutTokenIR &item) {
    const auto *record = find_record(records, item.logical_record);
    return record != nullptr && item.token_index < record->ir.tokens.size() &&
           item.encoded == record->ir.tokens[item.token_index].encoded &&
           item.bytes.begin ==
               record->ir.tokens[item.token_index].byte_range.begin &&
           item.bytes.end ==
               record->ir.tokens[item.token_index].byte_range.end;
  };
  const auto physical_row = [&](const MessageSourceRowIR &source)
      -> const PhysicalRowIR * {
    const auto run = std::find_if(layout.runs.begin(), layout.runs.end(),
                                  [&](const auto &item) {
                                    return item.id == source.first;
                                  });
    return run == layout.runs.end() || source.second >= run->rows.size()
               ? nullptr
               : &run->rows[source.second];
  };
  if (catalog.entries.size() != 396 || catalog.boundaries.size() != 792)
    return false;
  const auto check_paragraph = [&](const MessageParagraphIR &paragraph) {
    std::string semantic_text;
    for (const auto &row : paragraph.semantic_rows) {
      const auto *physical = physical_row(row.source_row);
      if (physical == nullptr)
        return false;
      if (row.terminal_layout_token &&
          !valid_terminal_token(*row.terminal_layout_token))
        return false;
      for (const auto &slice : row.leading_source_slices)
        if (!valid_slice(slice))
          return false;
      for (const auto &slice : row.trailing_source_slices)
        if (!valid_slice(slice))
          return false;
      if (row.marker_disposition ==
              MessageMarkerDispositionIR::lexical_prefix &&
          (!physical->marker ||
           row.text.rfind(physical->marker->decoded_text, 0) != 0))
        return false;
      if (row.marker_disposition ==
              MessageMarkerDispositionIR::closing_delimiter_bridge &&
          (row.leading_source_slices.empty() || !physical->marker))
        return false;
      if (!row.leading_source_slices.empty() && physical->marker) {
        const auto &slice = row.leading_source_slices.front();
        const auto *record = find_record(records, slice.logical_record);
        std::string prefix;
        if (record != nullptr) {
          std::vector<TokenWords> words(
              record->tokens.begin() +
                  static_cast<std::ptrdiff_t>(slice.token_begin),
              record->tokens.begin() +
                  static_cast<std::ptrdiff_t>(slice.token_end));
          prefix = collapse_ascii_whitespace(
              trim_ascii(token_words_to_ascii(assemble_logical_record(words))));
        }
        if (closes_unmatched_delimiter(prefix,
                                       physical->marker->decoded_text) &&
            row.marker_disposition !=
                MessageMarkerDispositionIR::closing_delimiter_bridge)
          return false;
      }
      if (!row.text.empty()) {
        if (!semantic_text.empty())
          semantic_text.push_back(' ');
        semantic_text += row.text;
      }
    }
    if (!paragraph.semantic_rows.empty() && paragraph.text != semantic_text)
      return false;
    for (const auto &slice : paragraph.source_slices)
      if (!valid_slice(slice))
        return false;
    for (const auto &item : paragraph.suppressed_layout_tokens)
      if (!valid_terminal_token(item))
        return false;
    return true;
  };
  for (std::size_t entry_index = 0; entry_index < catalog.entries.size();
       ++entry_index) {
    const auto &entry = catalog.entries[entry_index];
    if (entry.sections.size() != 2 ||
        entry.sections[0].kind != MessageSectionKind::meaning ||
        entry.sections[1].kind != MessageSectionKind::action)
      return false;
    for (std::size_t section_index = 0; section_index < 2; ++section_index) {
      const auto &section = entry.sections[section_index];
      if (!section.boundary_index ||
          *section.boundary_index != entry_index * 2 + section_index ||
          *section.boundary_index >= catalog.boundaries.size())
        return false;
      const auto &boundary = catalog.boundaries[*section.boundary_index];
      if (boundary.owner_entry != entry_index ||
          boundary.kind != section.kind || !valid_slice(boundary.label_source) ||
          section.recovered_record_continuation !=
              (boundary.shape != MessageSectionBoundaryShapeIR::normal_row) ||
          section.label_source_slices.size() != 1 ||
          !same_slice(section.label_source_slices.front(),
                      boundary.label_source))
        return false;
      const auto *label_record =
          find_record(records, boundary.label_source.logical_record);
      if (label_record == nullptr)
        return false;
      const auto expected_label =
          boundary.kind == MessageSectionKind::meaning ? "Meaning" : "Action";
      const auto &label_token =
          label_record->ir.tokens[boundary.label_source.token_begin];
      if (label_token.decoded_words.size() !=
              std::char_traits<char>::length(expected_label) ||
          !std::equal(label_token.decoded_words.begin(),
                      label_token.decoded_words.end(), expected_label,
                      [](const auto word, const auto character) {
                        return word == static_cast<unsigned char>(character);
                      }))
        return false;
      for (const auto &slice : boundary.payload_source_slices)
        if (!valid_slice(slice))
          return false;
      for (const auto &paragraph : section.paragraphs)
        if (!check_paragraph(paragraph))
          return false;
    }
    if (!check_paragraph(entry.headline))
      return false;
    for (const auto &paragraph : entry.headline_continuations)
      if (!check_paragraph(paragraph))
        return false;
  }
  return true;
}

bool verify_message_topic_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const VerifiedOwnershipIR &verified_ownership,
    const MessageTopicIR &topic, std::string *error) {
  const OwnershipIR &ownership = verified_ownership;
  if (!message_topic_candidate_is_consistent(topic))
    return fail(error, "message topic candidate invariants are inconsistent");
  if (!message_catalog_candidate_is_consistent(records, layout,
                                               topic.catalog))
    return fail(error, "message catalog candidate invariants are inconsistent");
  const auto envelope = extract_message_topic_ir_impl(
      records, layout, verified_ownership, false, error);
  if (!envelope || !same_topic_envelope(*envelope, topic))
    return fail(error, "message topic source envelope is inconsistent");
  if (!verify_message_catalog_ir(records, layout, ownership, topic.catalog,
                                 error))
    return false;
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
