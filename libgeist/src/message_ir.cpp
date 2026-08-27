#include "geist/detail/message_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace geist::detail {
namespace {

std::string range_text(const DecodedLogicalRecordSource &record,
                       const OutputRangeIR &range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size())
    return {};
  return text.substr(range.begin, range.end - range.begin);
}

std::string first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n");
  return value.substr(0, end);
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

const DecodedLogicalRecordSource *
find_record(const std::vector<DecodedLogicalRecordSource> &records,
            std::uint32_t logical_record);

std::optional<MessageSectionKind> section_kind(std::string value) {
  value = collapse_ascii_whitespace(trim_ascii(std::move(value)));
  // Lower-case single words carried in an SRMSG payload are compression
  // continuations from the preceding prose, not section labels. Canonical
  // labels in this catalog begin with an upper-case source character.
  if (value.empty() ||
      std::isupper(static_cast<unsigned char>(value.front())) == 0)
    return std::nullopt;
  value = ascii_lower(std::move(value));
  const auto matches = [&](const std::string &label) {
    return value == label || value == label + ":" ||
           ascii_starts_with_case_insensitive(value, label + ":");
  };
  if (matches("meaning"))
    return MessageSectionKind::meaning;
  if (matches("action"))
    return MessageSectionKind::action;
  return std::nullopt;
}

std::optional<MessageSectionKind> continuation_section_kind(
    const std::vector<DecodedLogicalRecordSource> &records,
    const PhysicalRowIR &row) {
  if (row.start != PhysicalRowStartKind::record_continuation)
    return std::nullopt;
  const auto *record = find_record(records, row.logical_record);
  if (record == nullptr || row.segment_index >= record->control_segments.size())
    return std::nullopt;
  const auto raw = token_words_to_ascii(record->assembled.words);
  const auto begin = record->control_segments[row.segment_index].complete.begin;
  if (begin > raw.size())
    return std::nullopt;
  return section_kind(raw.substr(0, begin));
}

bool source_slices_equal(const std::vector<DocumentSourceSliceIR> &left,
                         const std::vector<DocumentSourceSliceIR> &right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](const auto &x, const auto &y) {
                      return x.logical_record == y.logical_record &&
                             x.segment_index == y.segment_index &&
                             x.token_begin == y.token_begin &&
                             x.token_end == y.token_end &&
                             x.byte_begin == y.byte_begin &&
                             x.byte_end == y.byte_end;
                    });
}

bool section_equal(const MessageSectionIR &left,
                   const MessageSectionIR &right) {
  return left.kind == right.kind && left.run == right.run &&
         left.row == right.row && left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.recovered_record_continuation ==
             right.recovered_record_continuation &&
         left.label_source_rows == right.label_source_rows &&
         left.source_rows == right.source_rows &&
         left.paragraphs.size() == right.paragraphs.size() &&
         std::equal(left.paragraphs.begin(), left.paragraphs.end(),
                    right.paragraphs.begin(), [](const auto &a, const auto &b) {
                      return a.text == b.text &&
                             a.recovered_unformatted_segment ==
                                 b.recovered_unformatted_segment &&
                             a.source_rows == b.source_rows &&
                             a.source_segments == b.source_segments &&
                             source_slices_equal(a.source_slices,
                                                 b.source_slices) &&
                             a.semantic_rows == b.semantic_rows;
                    });
}

bool entry_equal(const MessageEntryIR &left, const MessageEntryIR &right) {
  if (left.id != right.id || left.logical_record != right.logical_record ||
      left.segment_index != right.segment_index ||
      left.headline.text != right.headline.text ||
      left.headline.recovered_unformatted_segment !=
          right.headline.recovered_unformatted_segment ||
      left.headline.source_rows != right.headline.source_rows ||
      left.headline.source_segments != right.headline.source_segments ||
      !source_slices_equal(left.headline.source_slices,
                           right.headline.source_slices) ||
      left.headline.semantic_rows != right.headline.semantic_rows ||
      left.headline_continuations.size() !=
          right.headline_continuations.size() ||
      left.sections.size() != right.sections.size())
    return false;
  for (std::size_t index = 0; index < left.headline_continuations.size();
       ++index)
    if (left.headline_continuations[index].text !=
            right.headline_continuations[index].text ||
        left.headline_continuations[index].recovered_unformatted_segment !=
            right.headline_continuations[index].recovered_unformatted_segment ||
        left.headline_continuations[index].source_rows !=
            right.headline_continuations[index].source_rows ||
        left.headline_continuations[index].source_segments !=
            right.headline_continuations[index].source_segments ||
        !source_slices_equal(
            left.headline_continuations[index].source_slices,
            right.headline_continuations[index].source_slices) ||
        left.headline_continuations[index].semantic_rows !=
            right.headline_continuations[index].semantic_rows)
      return false;
  if (left.source_rows != right.source_rows ||
      left.suppressed_source_rows != right.suppressed_source_rows)
    return false;
  for (std::size_t index = 0; index < left.sections.size(); ++index)
    if (!section_equal(left.sections[index], right.sections[index]))
      return false;
  return true;
}

const char *section_name(MessageSectionKind kind) {
  return kind == MessageSectionKind::meaning ? "meaning" : "action";
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

bool unmapped_cell(const DecodedLogicalRecordSource &record,
                   const OwnedSourceCellIR &cell) {
  if (cell.token_index >= record.ir.tokens.size())
    return false;
  const auto &unmapped =
      record.ir.tokens[cell.token_index].unmapped_word_indices;
  return std::find(unmapped.begin(), unmapped.end(), cell.word_index) !=
         unmapped.end();
}

using MessageCellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;

struct MessageOwnershipIndex {
  std::map<MessageSourceRowIR, std::vector<const OwnedSourceCellIR *>> rows;
  std::map<MessageCellKey, const OwnedSourceCellIR *> cells;
  std::map<std::uint32_t, const DecodedLogicalRecordSource *> records;
  std::map<std::uint32_t, std::vector<std::vector<std::size_t>>> token_outputs;
};

MessageOwnershipIndex
index_message_ownership(const std::vector<DecodedLogicalRecordSource> &records,
                        const OwnershipIR &ownership) {
  MessageOwnershipIndex result;
  for (const auto &record : records) {
    result.records.emplace(record.logical_record, &record);
    auto &outputs = result.token_outputs[record.logical_record];
    outputs.resize(record.ir.tokens.size());
    for (std::size_t output = 0; output < record.assembled.sources.size();
         ++output) {
      const auto token = record.assembled.sources[output].token_index;
      if (token < outputs.size())
        outputs[token].push_back(output);
    }
  }
  for (const auto &cell : ownership.cells) {
    result.cells.emplace(
        MessageCellKey{cell.logical_record, cell.token_index, cell.word_index},
        &cell);
    if (cell.run != 0)
      result.rows[{cell.run, cell.row_index}].push_back(&cell);
  }
  return result;
}

std::string owned_row_text(const MessageOwnershipIndex &ownership,
                           const PhysicalRowIR &row, std::size_t row_index,
                           bool suppress_terminal_layout_word = false,
                           std::optional<MessageTerminalLayoutTokenIR>
                               *terminal_layout_token = nullptr) {
  std::string result;
  const auto indexed_record = ownership.records.find(row.logical_record);
  const auto indexed_outputs = ownership.token_outputs.find(row.logical_record);
  if (indexed_record == ownership.records.end() ||
      indexed_outputs == ownership.token_outputs.end())
    return result;
  const auto *record = indexed_record->second;
  std::map<std::pair<std::size_t, std::size_t>, const OwnedSourceCellIR *>
      visible_cells;
  std::set<std::size_t> visible_tokens;
  const auto indexed_row = ownership.rows.find({row.run, row_index});
  if (indexed_row != ownership.rows.end()) {
    for (const auto *cell : indexed_row->second) {
      if (cell->logical_record != row.logical_record ||
          cell->disposition != SourceDisposition::visible_content)
        continue;
      visible_cells.emplace(std::make_pair(cell->token_index, cell->word_index),
                            cell);
      visible_tokens.insert(cell->token_index);
    }
  }
  std::optional<std::size_t> suppressed_token;
  if (suppress_terminal_layout_word && !visible_tokens.empty()) {
    const auto token = *visible_tokens.rbegin();
    if (token < record->ir.tokens.size()) {
      const auto &source = record->ir.tokens[token];
      const auto begin = source.has_spacing_control ? std::size_t{1} : 0;
      const auto alphabetic =
          begin < source.decoded_words.size() &&
          std::all_of(
              source.decoded_words.begin() + static_cast<std::ptrdiff_t>(begin),
              source.decoded_words.end(),
              [](const auto word) {
                return word <= 0xff &&
                       std::isalpha(static_cast<unsigned char>(word)) != 0;
              });
      const auto decoded = trim_ascii(token_words_to_ascii(source.decoded_words));
      const auto terminal_punctuation =
          decoded == "<" ||
          (decoded == ">" &&
           std::count(row.visible_text.begin(), row.visible_text.end(), '<') <
               std::count(row.visible_text.begin(), row.visible_text.end(),
                          '>'));
      // Compact values 19..43 are the terminal row-control alphabet only at
      // this exact pre-section boundary. Preserve closing `>` when it balances
      // a source placeholder on the same row; an otherwise isolated `<`/`>`
      // is layout punctuation.
      if (source.encoded.width == 1 && source.encoded.value >= 19 &&
          source.encoded.value <= 43 &&
          (alphabetic || terminal_punctuation)) {
        suppressed_token = token;
        if (terminal_layout_token != nullptr) {
          MessageTerminalLayoutTokenIR evidence;
          evidence.logical_record = row.logical_record;
          evidence.token_index = token;
          evidence.encoded = source.encoded;
          evidence.bytes = source.byte_range;
          evidence.decoded_text = decoded;
          *terminal_layout_token = std::move(evidence);
        }
      }
    }
  }
  const auto token_end =
      std::min(row.token_end, indexed_outputs->second.size());
  bool pending_space = false;
  for (auto token = row.token_begin; token < token_end; ++token) {
    if (suppressed_token == token)
      continue;
    for (const auto output : indexed_outputs->second[token]) {
      const auto &source = record->assembled.sources[output];
      if (source.kind == LogicalWordSourceKind::inserted_space) {
        pending_space = true;
        continue;
      }
      const auto found =
          visible_cells.find({source.token_index, source.word_index});
      if (found == visible_cells.end()) {
        const auto owned = ownership.cells.find(
            {row.logical_record, source.token_index, source.word_index});
        if (owned != ownership.cells.end() &&
            (owned->second->disposition == SourceDisposition::layout_padding ||
             owned->second->disposition == SourceDisposition::layout_origin))
          pending_space = true;
        continue;
      }
      if (found->second->word > 0xff || unmapped_cell(*record, *found->second))
        continue;
      const auto ch = static_cast<char>(found->second->word);
      if (pending_space && !result.empty() && result.back() != ' ' &&
          (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '<' ||
           ch == '/' || ch == '(' || ch == '-' || ch == '|' || ch == '='))
        result.push_back(' ');
      result.push_back(ch);
      pending_space = false;
    }
  }
  return result;
}

bool structural_padding_token(const LogicalTokenIR &token) {
  const auto begin = token.has_spacing_control ? std::size_t{1} : 0;
  return begin < token.decoded_words.size() &&
         std::all_of(
             token.decoded_words.begin() + static_cast<std::ptrdiff_t>(begin),
             token.decoded_words.end(),
             [](const auto word) {
               return word == ' ' || word == '?' || word == 0x2666 ||
                      word < 0x20;
             });
}

std::string opaque_continuation_prefix(
    const std::vector<DecodedLogicalRecordSource> &records,
    const MessageOwnershipIndex &ownership, const PhysicalRowIR &row) {
  if (row.start != PhysicalRowStartKind::record_continuation)
    return {};
  const auto *record = find_record(records, row.logical_record);
  if (record == nullptr || row.segment_index >= record->control_segments.size())
    return {};
  const auto &segment = record->control_segments[row.segment_index];
  const auto range = decoded_byte_range_to_word_range(record->assembled,
                                                      segment.payload_range);
  auto token_end = row.token_begin;
  for (std::size_t index = row.segment_index + 1;
       index < record->control_segments.size(); ++index)
    if (!record->control_segments[index].source_tokens.empty())
      token_end = std::min(
          token_end, record->control_segments[index].source_tokens.front());
  std::string result;
  for (std::size_t output = range.begin;
       output < range.end && output < record->assembled.sources.size();
       ++output) {
    const auto &source = record->assembled.sources[output];
    if (source.token_index >= token_end ||
        source.token_index >= record->ir.tokens.size() ||
        structural_padding_token(record->ir.tokens[source.token_index]))
      continue;
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      result.push_back(' ');
      continue;
    }
    const auto found = ownership.cells.find(
        {row.logical_record, source.token_index, source.word_index});
    if (found == ownership.cells.end() ||
        found->second->disposition != SourceDisposition::opaque ||
        found->second->word > 0xff || unmapped_cell(*record, *found->second))
      continue;
    result.push_back(static_cast<char>(found->second->word));
  }
  return collapse_ascii_whitespace(trim_ascii(std::move(result)));
}

std::string
opaque_segment_suffix(const std::vector<DecodedLogicalRecordSource> &records,
                      const MessageOwnershipIndex &ownership,
                      const PhysicalRowIR &row) {
  const auto *record = find_record(records, row.logical_record);
  if (record == nullptr || row.segment_index >= record->control_segments.size())
    return {};
  const auto &segment = record->control_segments[row.segment_index];
  const auto range = decoded_byte_range_to_word_range(record->assembled,
                                                      segment.payload_range);
  std::string result;
  for (std::size_t output = range.begin;
       output < range.end && output < record->assembled.sources.size();
       ++output) {
    const auto &source = record->assembled.sources[output];
    if (source.token_index < row.token_end ||
        source.token_index >= record->ir.tokens.size() ||
        structural_padding_token(record->ir.tokens[source.token_index]))
      continue;
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      result.push_back(' ');
      continue;
    }
    const auto found = ownership.cells.find(
        {row.logical_record, source.token_index, source.word_index});
    if (found == ownership.cells.end() ||
        found->second->disposition != SourceDisposition::opaque ||
        found->second->word > 0xff || unmapped_cell(*record, *found->second))
      continue;
    result.push_back(static_cast<char>(found->second->word));
  }
  return collapse_ascii_whitespace(trim_ascii(std::move(result)));
}

struct OpaqueSegmentFragment {
  std::string text;
  DocumentSourceSliceIR source;
};

std::vector<OpaqueSegmentFragment>
opaque_segment_fragments(const DecodedLogicalRecordSource &record,
                         const MessageOwnershipIndex &ownership,
                         const ControlSegmentIR &segment) {
  const auto range =
      decoded_byte_range_to_word_range(record.assembled, segment.payload_range);
  std::optional<std::size_t> terminal_layout_token;
  for (std::size_t output = range.begin;
       output < range.end && output < record.assembled.sources.size();
       ++output) {
    const auto &source = record.assembled.sources[output];
    const auto found = ownership.cells.find(
        {record.logical_record, source.token_index, source.word_index});
    if (found != ownership.cells.end() &&
        found->second->disposition == SourceDisposition::opaque)
      terminal_layout_token = source.token_index;
  }
  if (terminal_layout_token &&
      *terminal_layout_token < record.ir.tokens.size()) {
    const auto &token = record.ir.tokens[*terminal_layout_token];
    // Fixed display fragments terminate immediately before the next typed
    // control. At that exact boundary compact values 19..43 are the observed
    // row-control alphabet (including token-map spellings such as "and" and
    // "agent"), not prose. Preserve the same spellings everywhere else.
    if (token.encoded.width != 1 || token.encoded.value < 19 ||
        token.encoded.value > 43)
      terminal_layout_token.reset();
  }
  std::vector<OpaqueSegmentFragment> result;
  std::string text;
  std::optional<std::size_t> first_token;
  std::optional<std::size_t> last_token;
  const auto finish = [&] {
    if (text.empty()) {
      first_token.reset();
      last_token.reset();
      return;
    }
    text = collapse_ascii_whitespace(trim_ascii(std::move(text)));
    if (!text.empty() && first_token && last_token) {
      const auto &first = record.ir.tokens[*first_token];
      const auto &last = record.ir.tokens[*last_token];
      result.push_back(
          {std::move(text),
           {record.logical_record, segment.segment_index, *first_token,
            *last_token + 1, first.byte_range.begin, last.byte_range.end}});
    }
    text.clear();
    first_token.reset();
    last_token.reset();
  };
  for (std::size_t output = range.begin;
       output < range.end && output < record.assembled.sources.size();
       ++output) {
    const auto &source = record.assembled.sources[output];
    if (source.token_index >= record.ir.tokens.size() ||
        (terminal_layout_token &&
         source.token_index == *terminal_layout_token) ||
        structural_padding_token(record.ir.tokens[source.token_index])) {
      if (!text.empty())
        finish();
      continue;
    }
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      if (!text.empty())
        text.push_back(' ');
      continue;
    }
    const auto found = ownership.cells.find(
        {record.logical_record, source.token_index, source.word_index});
    if (found == ownership.cells.end() ||
        found->second->disposition != SourceDisposition::opaque ||
        found->second->word > 0xff || unmapped_cell(record, *found->second)) {
      if (!text.empty())
        finish();
      continue;
    }
    if (!first_token)
      first_token = source.token_index;
    last_token = source.token_index;
    text.push_back(static_cast<char>(found->second->word));
  }
  finish();
  return result;
}

std::string complete_segment_text(const DecodedLogicalRecordSource &record,
                                  const ControlSegmentIR &segment,
                                  DocumentSourceSliceIR *source_slice) {
  auto result = collapse_ascii_whitespace(
      trim_ascii(range_text(record, segment.complete)));
  if (result.empty() || source_slice == nullptr ||
      segment.source_tokens.empty())
    return result;
  const auto first_token = segment.source_tokens.front();
  const auto last_token = segment.source_tokens.back();
  if (first_token >= record.ir.tokens.size() ||
      last_token >= record.ir.tokens.size())
    return result;
  const auto &first = record.ir.tokens[first_token];
  const auto &last = record.ir.tokens[last_token];
  *source_slice = {
      record.logical_record, segment.segment_index,  first_token,
      last_token + 1,        first.byte_range.begin, last.byte_range.end};
  return result;
}

std::string opaque_text_before_segment(
    const std::vector<DecodedLogicalRecordSource> &records,
    const MessageOwnershipIndex &ownership, std::uint32_t logical_record,
    std::size_t segment_index, DocumentSourceSliceIR *source_slice = nullptr) {
  const auto *record = find_record(records, logical_record);
  if (record == nullptr || segment_index == 0 ||
      segment_index >= record->control_segments.size())
    return {};
  const auto previous_end =
      record->control_segments[segment_index - 1].complete.end;
  const auto current_begin =
      record->control_segments[segment_index].complete.begin;
  if (previous_end > current_begin)
    return {};
  const auto range = decoded_byte_range_to_word_range(
      record->assembled, {previous_end, current_begin});
  std::string result;
  std::optional<std::size_t> first_token;
  std::optional<std::size_t> last_token;
  for (std::size_t output = range.begin;
       output < range.end && output < record->assembled.sources.size();
       ++output) {
    const auto &source = record->assembled.sources[output];
    if (source.token_index >= record->ir.tokens.size() ||
        structural_padding_token(record->ir.tokens[source.token_index]))
      continue;
    if (source.kind == LogicalWordSourceKind::inserted_space) {
      result.push_back(' ');
      continue;
    }
    const auto found = ownership.cells.find(
        {logical_record, source.token_index, source.word_index});
    if (found == ownership.cells.end() || found->second->word > 0xff ||
        unmapped_cell(*record, *found->second))
      continue;
    const auto marker_word = static_cast<unsigned char>(found->second->word);
    const auto visible_opaque =
        found->second->disposition == SourceDisposition::opaque;
    const auto alphabetic_marker =
        found->second->disposition == SourceDisposition::marker_slot &&
        std::isalpha(marker_word) != 0;
    if (!visible_opaque && !alphabetic_marker)
      continue;
    if (!first_token)
      first_token = source.token_index;
    last_token = source.token_index;
    result.push_back(static_cast<char>(found->second->word));
  }
  result = collapse_ascii_whitespace(trim_ascii(std::move(result)));
  if (!result.empty() && source_slice != nullptr && first_token && last_token) {
    const auto &first = record->ir.tokens[*first_token];
    const auto &last = record->ir.tokens[*last_token];
    *source_slice = {logical_record,         segment_index,
                     *first_token,           *last_token + 1,
                     first.byte_range.begin, last.byte_range.end};
  }
  return result;
}

MessageMarkerDispositionIR marker_disposition(const PhysicalRowIR &row,
                                              bool section_label,
                                              std::size_t row_index,
                                              bool has_opaque_prefix) {
  if (!row.marker)
    return MessageMarkerDispositionIR::absent;
  if (section_label)
    return MessageMarkerDispositionIR::layout_artifact;
  const auto &marker = *row.marker;
  const auto single = marker.decoded_text.size() == 1
                          ? static_cast<unsigned char>(marker.decoded_text[0])
                          : 0;
  if (single == '-' || single == '<' || single == '>' || single == '/' ||
      single == '"' || single == '=' || single == '(' || single == ')' ||
      single == '[' || single == '{')
    return MessageMarkerDispositionIR::layout_artifact;
  if (single == '.' || single == ',' || single == ':' || single == ';' ||
      single == '!' || single == '?' || single == ']' || single == '}') {
    // Value 4 is the catalog's mechanical soft-wrap placeholder, even though
    // its token-map projection is a question mark.
    if (marker.encoded_value == 4)
      return MessageMarkerDispositionIR::layout_artifact;
    // A punctuation token at the first explicit marker slot belongs to the
    // fixed-row envelope. Later marker slots close the preceding semantic row.
    if (row_index == 0 &&
        row.start == PhysicalRowStartKind::explicit_marker_slot &&
        !row.continues_previous_record)
      return MessageMarkerDispositionIR::layout_artifact;
    return MessageMarkerDispositionIR::punctuation_suffix;
  }
  const auto lexical =
      !marker.decoded_text.empty() &&
      std::all_of(marker.decoded_text.begin(), marker.decoded_text.end(),
                  [](const unsigned char ch) {
                    return std::isalnum(ch) != 0 || ch == '_';
                  });
  if (lexical) {
    if (has_opaque_prefix)
      return MessageMarkerDispositionIR::opaque_continuation_suffix;
    // Compact dictionary values 28..43 are the observed fixed-row marker
    // alphabet only at the first explicit marker slot of a new run. The same
    // spelling elsewhere is lexical; high dictionary values such as MSG023's
    // "The" are lexical even after terminal punctuation.
    if (row_index == 0 &&
        row.start == PhysicalRowStartKind::explicit_marker_slot &&
        !row.continues_previous_record && marker.encoded_width == 1 &&
        marker.encoded_value >= 28 && marker.encoded_value <= 43)
      return MessageMarkerDispositionIR::layout_artifact;
    return MessageMarkerDispositionIR::lexical_prefix;
  }
  return MessageMarkerDispositionIR::layout_artifact;
}

MessageSemanticRowIR
semantic_row(const std::vector<DecodedLogicalRecordSource> &records,
             const MessageOwnershipIndex &ownership, const PhysicalRowIR &row,
             DisplayRunId run, std::size_t row_index, bool section_label,
             bool suppress_terminal_layout_word = false,
             bool recover_segment_suffix = false) {
  MessageSemanticRowIR result;
  result.source_row = {run, row_index};
  auto visible = collapse_ascii_whitespace(trim_ascii(
      owned_row_text(ownership, row, row_index, suppress_terminal_layout_word,
                     &result.terminal_layout_token)));
  const auto prefix = opaque_continuation_prefix(records, ownership, row);
  result.marker_disposition =
      marker_disposition(row, section_label, row_index, !prefix.empty());
  if (row.marker &&
      result.marker_disposition ==
          MessageMarkerDispositionIR::opaque_continuation_suffix) {
    visible = prefix + " " + row.marker->decoded_text +
              (visible.empty() ? std::string{} : std::string{" "}) + visible;
  } else {
    if (!prefix.empty())
      visible = prefix + (visible.empty() ? std::string{} : std::string{" "}) +
                visible;
  }
  if (row.marker &&
      result.marker_disposition == MessageMarkerDispositionIR::lexical_prefix)
    visible = row.marker->decoded_text +
              (visible.empty() ? std::string{} : std::string{" "}) + visible;
  else if (row.marker &&
           result.marker_disposition == MessageMarkerDispositionIR::list_prefix)
    visible = row.marker->decoded_text +
              (visible.empty() ? std::string{} : std::string{" "}) + visible;
  if (recover_segment_suffix) {
    const auto suffix = opaque_segment_suffix(records, ownership, row);
    if (!suffix.empty())
      visible += (visible.empty() ? std::string{} : std::string{" "}) + suffix;
  }
  result.text = std::move(visible);
  return result;
}

void append_text(std::string &destination, const std::string &text) {
  if (text.empty())
    return;
  if (!destination.empty())
    destination += ' ';
  destination += text;
}

MessageParagraphIR
paragraph_for_segment(const std::vector<DecodedLogicalRecordSource> &records,
                      const LayoutIR &layout,
                      const MessageOwnershipIndex &ownership,
                      std::uint32_t logical_record, std::size_t segment_index) {
  MessageParagraphIR paragraph;
  for (const auto &run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      if (row.logical_record != logical_record ||
          row.segment_index != segment_index)
        continue;
      auto semantic =
          semantic_row(records, ownership, row, run.id, row_index, false);
      if (row.marker &&
          semantic.marker_disposition ==
              MessageMarkerDispositionIR::punctuation_suffix &&
          (paragraph.text.empty() ||
           paragraph.text.back() != row.marker->decoded_text.front()))
        paragraph.text += row.marker->decoded_text;
      append_text(paragraph.text, semantic.text);
      paragraph.source_rows.push_back({run.id, row_index});
      paragraph.semantic_rows.push_back(std::move(semantic));
      const auto segment =
          std::make_pair(row.logical_record, row.segment_index);
      if (paragraph.source_segments.empty() ||
          paragraph.source_segments.back() != segment)
        paragraph.source_segments.push_back(segment);
    }
  }
  return paragraph;
}

MessageParagraphIR paragraph_before_segment(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const MessageOwnershipIndex &ownership,
    std::uint32_t logical_record, std::size_t segment_index) {
  MessageParagraphIR paragraph;
  for (const auto &run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      if (row.logical_record != logical_record ||
          row.segment_index >= segment_index)
        continue;
      auto semantic =
          semantic_row(records, ownership, row, run.id, row_index, false);
      if (row.marker &&
          semantic.marker_disposition ==
              MessageMarkerDispositionIR::punctuation_suffix &&
          (paragraph.text.empty() ||
           paragraph.text.back() != row.marker->decoded_text.front()))
        paragraph.text += row.marker->decoded_text;
      append_text(paragraph.text, semantic.text);
      paragraph.source_rows.push_back({run.id, row_index});
      paragraph.semantic_rows.push_back(std::move(semantic));
      const auto segment =
          std::make_pair(row.logical_record, row.segment_index);
      if (paragraph.source_segments.empty() ||
          paragraph.source_segments.back() != segment)
        paragraph.source_segments.push_back(segment);
    }
  }
  return paragraph;
}

void remove_body_rows(MessageEntryIR &entry,
                      const std::vector<MessageSourceRowIR> &rows) {
  entry.headline_continuations.erase(
      std::remove_if(entry.headline_continuations.begin(),
                     entry.headline_continuations.end(),
                     [&](const auto &paragraph) {
                       return std::any_of(
                           paragraph.source_rows.begin(),
                           paragraph.source_rows.end(), [&](const auto &row) {
                             return std::find(rows.begin(), rows.end(), row) !=
                                    rows.end();
                           });
                     }),
      entry.headline_continuations.end());
}

void remove_section_rows(MessageSectionIR &section,
                         const std::vector<MessageSourceRowIR> &rows) {
  const auto remove = [&](auto &values) {
    values.erase(std::remove_if(values.begin(), values.end(),
                                [&](const auto &value) {
                                  return std::find(rows.begin(), rows.end(),
                                                   value) != rows.end();
                                }),
                 values.end());
  };
  remove(section.source_rows);
  for (auto &paragraph : section.paragraphs) {
    remove(paragraph.source_rows);
    paragraph.semantic_rows.erase(
        std::remove_if(paragraph.semantic_rows.begin(),
                       paragraph.semantic_rows.end(),
                       [&](const auto &row) {
                         return std::find(rows.begin(), rows.end(),
                                          row.source_row) != rows.end();
                       }),
        paragraph.semantic_rows.end());
  }
}

std::string section_payload(MessageSectionKind kind, std::string text) {
  text = collapse_ascii_whitespace(trim_ascii(std::move(text)));
  const auto label = std::string(section_name(kind));
  if (!ascii_starts_with_case_insensitive(text, label))
    return text;
  auto begin = label.size();
  if (begin < text.size() && text[begin] == ':')
    ++begin;
  return trim_ascii(text.substr(begin));
}

std::string clean_boundary_continuation(std::string value) {
  value = collapse_ascii_whitespace(trim_ascii(std::move(value)));
  const auto lexical = value.find_first_of(
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
  if (lexical != std::string::npos)
    value.erase(0, lexical);
  for (auto marker = value.find(" ? "); marker != std::string::npos;
       marker = value.find(" ? ", marker))
    value.erase(marker, 2);
  return collapse_ascii_whitespace(trim_ascii(std::move(value)));
}

std::tuple<std::uint32_t, std::size_t, std::size_t>
first_source_coordinate(const LayoutIR &layout,
                        const MessageParagraphIR &paragraph) {
  if (!paragraph.source_rows.empty()) {
    const auto source = paragraph.source_rows.front();
    const auto run =
        std::find_if(layout.runs.begin(), layout.runs.end(),
                     [&](const auto &item) { return item.id == source.first; });
    if (run != layout.runs.end() && source.second < run->rows.size()) {
      const auto &row = run->rows[source.second];
      return {row.logical_record, row.segment_index, row.token_begin};
    }
  }
  if (!paragraph.source_slices.empty())
    return {paragraph.source_slices.front().logical_record,
            paragraph.source_slices.front().segment_index,
            paragraph.source_slices.front().token_begin};
  if (!paragraph.source_segments.empty())
    return {paragraph.source_segments.front().first,
            paragraph.source_segments.front().second, 0};
  return {std::numeric_limits<std::uint32_t>::max(),
          std::numeric_limits<std::size_t>::max(),
          std::numeric_limits<std::size_t>::max()};
}

} // namespace

std::optional<MessageCatalogIR> extract_message_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership, std::string *error) {
  const auto fail =
      [&](const std::string &message) -> std::optional<MessageCatalogIR> {
    if (error != nullptr)
      *error = message;
    return std::nullopt;
  };
  if (records.empty() || !ownership.conflicts.empty())
    return fail("source ownership is unavailable or conflicted");
  const auto ownership_index = index_message_ownership(records, ownership);

  using SegmentKey = std::pair<std::uint32_t, std::size_t>;
  using SegmentRow =
      std::tuple<MessageSourceRowIR, std::size_t, std::size_t>;
  std::map<SegmentKey, std::vector<SegmentRow>> rows_by_segment;
  for (const auto &run : layout.runs)
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      rows_by_segment[{row.logical_record, row.segment_index}].push_back(
          {{run.id, row_index}, row.token_begin, row.token_end});
    }
  std::map<SegmentKey, std::size_t> entry_by_segment;
  MessageCatalogIR catalog;
  std::optional<std::size_t> active;
  bool saw_nonnumeric_message = false;
  for (const auto &record : records) {
    for (const auto &segment : record.control_segments) {
      if (segment.kind == BookControlKind::message_start) {
        // Text physically carried in an SRMSG control's own payload precedes
        // the new entry. It is overflow from the prior entry, so retain that
        // ownership before advancing the active message.
        if (active)
          entry_by_segment[{record.logical_record, segment.segment_index}] =
              *active;
        const auto id = first_word(range_text(record, segment.operand_range));
        active.reset();
        if (numeric_message_id(id)) {
          MessageEntryIR entry;
          entry.id = id;
          entry.logical_record = record.logical_record;
          entry.segment_index = segment.segment_index;
          catalog.entries.push_back(std::move(entry));
          active = catalog.entries.size() - 1;
          entry_by_segment[{record.logical_record, segment.segment_index}] =
              *active;
        } else if (!id.empty()) {
          saw_nonnumeric_message = true;
        }
      } else if (active) {
        entry_by_segment[{record.logical_record, segment.segment_index}] =
            *active;
      }
    }
  }
  if (catalog.entries.empty() || saw_nonnumeric_message)
    return fail("source is not one numeric message catalog");

  std::vector<std::optional<std::size_t>> active_section(
      catalog.entries.size());
  for (const auto &run : layout.runs) {
    std::optional<std::size_t> run_owner;
    for (const auto &row : run.rows) {
      const auto owner =
          entry_by_segment.find({row.logical_record, row.segment_index});
      if (owner != entry_by_segment.end()) {
        if (run_owner && *run_owner != owner->second)
          return fail("one display run crosses message entry ownership");
        run_owner = owner->second;
      }
    }
    if (!run_owner)
      continue;
    auto &entry = catalog.entries[*run_owner];
    if (run.control_kind == BookControlKind::structural ||
        run.control_kind == BookControlKind::unknown) {
      for (std::size_t row_index = 0; row_index < run.rows.size();
           ++row_index) {
        const MessageSourceRowIR source{run.id, row_index};
        entry.source_rows.push_back(source);
        entry.suppressed_source_rows.push_back(source);
      }
      continue;
    }

    std::optional<MessageSectionKind> run_kind;
    std::optional<std::size_t> label_row_index;
    bool explicit_label = false;
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      const auto owner =
          entry_by_segment.find({row.logical_record, row.segment_index});
      if (owner == entry_by_segment.end())
        continue;
      const auto visible = owned_row_text(ownership_index, row, row_index);
      auto kind = section_kind(visible);
      const auto row_has_explicit_label = kind.has_value();
      if (!kind)
        kind = continuation_section_kind(records, row);
      if (kind) {
        if (run_kind && *run_kind != *kind)
          return fail("one display run contains two message sections");
        run_kind = kind;
        if (!label_row_index) {
          label_row_index = row_index;
          explicit_label = row_has_explicit_label;
        }
      }
    }
    if (!run_kind && active_section[*run_owner] && !run.rows.empty() &&
        entry.sections[*active_section[*run_owner]].kind ==
            MessageSectionKind::meaning &&
        !entry.sections[*active_section[*run_owner]]
             .paragraphs.back()
             .text.empty() &&
        run.rows.front().marker &&
        run.rows.front().marker->decoded_text == ":") {
      run_kind = MessageSectionKind::action;
      label_row_index = 0;
      explicit_label = false;
    }
    if (run_kind) {
      if (entry.headline.text.find(entry.id) == std::string::npos) {
        const auto &source_row = run.rows[*label_row_index];
        auto recovered = opaque_text_before_segment(records, ownership_index,
                                                    source_row.logical_record,
                                                    source_row.segment_index);
        if (!recovered.empty()) {
          entry.suppressed_source_rows.insert(
              entry.suppressed_source_rows.end(),
              entry.headline.source_rows.begin(),
              entry.headline.source_rows.end());
          entry.headline.text = std::move(recovered);
          entry.headline.source_rows.clear();
          entry.headline.semantic_rows.clear();
          entry.headline.source_segments.push_back(
              {source_row.logical_record, source_row.segment_index - 1});
        }
      }
      auto &sections = entry.sections;
      if (std::any_of(
              sections.begin(), sections.end(),
              [&](const auto &section) { return section.kind == *run_kind; }))
        return fail("message entry has a duplicate semantic section: " +
                    entry.id);
      const auto row_index = *label_row_index;
      const auto &label_row = run.rows[row_index];
      MessageSectionIR section;
      section.kind = *run_kind;
      section.run = run.id;
      section.row = row_index;
      section.logical_record = label_row.logical_record;
      section.segment_index = label_row.segment_index;
      section.label_source_rows.push_back({run.id, row_index});
      section.paragraphs.push_back({});
      entry.sections.push_back(std::move(section));
      active_section[*run_owner] = entry.sections.size() - 1;
    }

    MessageParagraphIR run_paragraph;
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      const auto owner =
          entry_by_segment.find({row.logical_record, row.segment_index});
      if (owner == entry_by_segment.end())
        return fail("message run contains an unowned visible row");
      const MessageSourceRowIR source{run.id, row_index};
      entry.source_rows.push_back(source);
      const auto is_explicit_label = run_kind && label_row_index == row_index;
      const auto is_label =
          run_kind && (is_explicit_label || run_paragraph.text.empty());
      const auto final_segment_row =
          row_index + 1 == run.rows.size() ||
          run.rows[row_index + 1].logical_record != row.logical_record ||
          run.rows[row_index + 1].segment_index != row.segment_index;
      const auto structured_row =
          row.visible_text.find("    ") != std::string::npos;
      auto semantic = semantic_row(
          records, ownership_index, row, run.id, row_index, is_label,
          final_segment_row &&
              (structured_row || (!active_section[*run_owner] && !run_kind &&
                                  row_index + 1 == run.rows.size())),
          final_segment_row);
      auto text = semantic.text;
      if (run_kind && is_explicit_label && explicit_label)
        text = section_payload(*run_kind, std::move(text));
      if (row.marker &&
          semantic.marker_disposition ==
              MessageMarkerDispositionIR::punctuation_suffix &&
          (run_paragraph.text.empty() ||
           run_paragraph.text.back() != row.marker->decoded_text.front())) {
        run_paragraph.text += row.marker->decoded_text;
        if (!run_paragraph.semantic_rows.empty())
          run_paragraph.semantic_rows.back().text += row.marker->decoded_text;
      }
      append_text(run_paragraph.text, text);
      run_paragraph.source_rows.push_back(source);
      semantic.text = std::move(text);
      run_paragraph.semantic_rows.push_back(std::move(semantic));
      const auto segment =
          std::make_pair(row.logical_record, row.segment_index);
      if (run_paragraph.source_segments.empty() ||
          run_paragraph.source_segments.back() != segment)
        run_paragraph.source_segments.push_back(segment);
    }
    if (run_paragraph.text.empty()) {
      entry.suppressed_source_rows.insert(entry.suppressed_source_rows.end(),
                                          run_paragraph.source_rows.begin(),
                                          run_paragraph.source_rows.end());
      continue;
    }
    std::vector<MessageParagraphIR> row_paragraphs;
    for (const auto &semantic : run_paragraph.semantic_rows) {
      if (semantic.text.empty())
        continue;
      MessageParagraphIR paragraph;
      paragraph.text = semantic.text;
      paragraph.source_rows.push_back(semantic.source_row);
      paragraph.semantic_rows.push_back(semantic);
      if (semantic.source_row.second < run.rows.size()) {
        const auto &source_row = run.rows[semantic.source_row.second];
        paragraph.source_segments.push_back(
            {source_row.logical_record, source_row.segment_index});
      }
      row_paragraphs.push_back(std::move(paragraph));
    }
    if (active_section[*run_owner]) {
      auto &section = entry.sections[*active_section[*run_owner]];
      section.paragraphs.insert(section.paragraphs.end(),
                                std::make_move_iterator(row_paragraphs.begin()),
                                std::make_move_iterator(row_paragraphs.end()));
      section.source_rows.insert(section.source_rows.end(),
                                 run_paragraph.source_rows.begin(),
                                 run_paragraph.source_rows.end());
    } else {
      for (auto &paragraph : row_paragraphs) {
        if (entry.headline.source_rows.empty())
          entry.headline = std::move(paragraph);
        else
          entry.headline_continuations.push_back(std::move(paragraph));
      }
    }
  }

  for (std::size_t entry_index = 0; entry_index < catalog.entries.size();
       ++entry_index) {
    auto &entry = catalog.entries[entry_index];
    if (entry.sections.size() == 1 &&
        entry.sections.front().kind == MessageSectionKind::action) {
      const auto &action = entry.sections.front();
      const auto source =
          std::find_if(records.begin(), records.end(), [&](const auto &record) {
            return record.logical_record == action.logical_record;
          });
      const auto text =
          source == records.end()
              ? std::string{}
              : trim_ascii(token_words_to_ascii(source->assembled.words));
      if (source != records.end() && action.segment_index > 0 &&
          ascii_starts_with_case_insensitive(text, "meaning:")) {
        auto paragraph = paragraph_before_segment(
            records, layout, ownership_index, action.logical_record,
            action.segment_index);
        if (paragraph.text.empty()) {
          const auto *source = find_record(records, action.logical_record);
          if (source != nullptr) {
            const auto complete =
                source->control_segments[action.segment_index].complete.begin;
            const auto raw = token_words_to_ascii(source->assembled.words);
            paragraph.text =
                collapse_ascii_whitespace(trim_ascii(raw.substr(0, complete)));
            if (paragraph.text.empty()) {
              auto end = ascii_lower(raw).find("action:");
              if (end == std::string::npos)
                end = raw.size();
              paragraph.text =
                  collapse_ascii_whitespace(trim_ascii(raw.substr(0, end)));
            }
            paragraph.source_segments.push_back({action.logical_record, 0});
          }
        }
        paragraph.text = section_payload(MessageSectionKind::meaning,
                                         std::move(paragraph.text));
        if (paragraph.text.empty() && !entry.headline_continuations.empty()) {
          paragraph = std::move(entry.headline_continuations.back());
          entry.headline_continuations.pop_back();
          paragraph.text = section_payload(MessageSectionKind::meaning,
                                           std::move(paragraph.text));
        }
        remove_body_rows(entry, paragraph.source_rows);
        MessageSectionIR section;
        section.kind = MessageSectionKind::meaning;
        section.logical_record = action.logical_record;
        section.recovered_record_continuation = true;
        section.source_rows = paragraph.source_rows;
        section.label_source_rows = paragraph.source_rows;
        section.paragraphs.push_back(std::move(paragraph));
        entry.sections.insert(entry.sections.begin(), std::move(section));
      }
    } else if (entry.sections.size() == 1 &&
               entry.sections.front().kind == MessageSectionKind::meaning) {
      const auto &meaning = entry.sections.front();
      auto source =
          std::find_if(records.begin(), records.end(), [&](const auto &record) {
            return record.logical_record > meaning.logical_record;
          });
      for (; source != records.end(); ++source) {
        const auto starts_next_message = std::any_of(
            source->control_segments.begin(), source->control_segments.end(),
            [](const auto &segment) {
              return segment.kind == BookControlKind::message_start;
            });
        const auto text =
            trim_ascii(token_words_to_ascii(source->assembled.words));
        if (ascii_starts_with_case_insensitive(text, "action:"))
          break;
        if (starts_next_message) {
          source = records.end();
          break;
        }
      }
      if (source != records.end()) {
        auto paragraph = paragraph_for_segment(records, layout, ownership_index,
                                               source->logical_record, 0);
        if (paragraph.text.empty() && !source->control_segments.empty()) {
          paragraph.text = collapse_ascii_whitespace(trim_ascii(range_text(
              *source, source->control_segments.front().payload_range)));
          paragraph.source_segments.push_back({source->logical_record, 0});
        }
        paragraph.text = section_payload(MessageSectionKind::action,
                                         std::move(paragraph.text));
        if (paragraph.text.empty() && !entry.headline_continuations.empty()) {
          paragraph = std::move(entry.headline_continuations.back());
          entry.headline_continuations.pop_back();
          paragraph.text = section_payload(MessageSectionKind::action,
                                           std::move(paragraph.text));
        }
        if (paragraph.text.empty()) {
          const auto continuation = std::find_if(
              records.begin(), records.end(), [&](const auto &record) {
                return record.logical_record > source->logical_record;
              });
          if (continuation != records.end()) {
            const auto boundary = std::find_if(
                continuation->control_segments.begin(),
                continuation->control_segments.end(), [](const auto &segment) {
                  return segment.kind == BookControlKind::message_start;
                });
            if (boundary != continuation->control_segments.end() &&
                boundary->segment_index == 1 &&
                continuation->control_segments.front().kind ==
                    BookControlKind::text) {
              paragraph.text = collapse_ascii_whitespace(trim_ascii(range_text(
                  *continuation,
                  continuation->control_segments.front().payload_range)));
              if (!paragraph.text.empty())
                paragraph.source_segments.push_back(
                    {continuation->logical_record, 0});
            }
          }
        }
        MessageSectionIR section;
        section.kind = MessageSectionKind::action;
        section.logical_record = source->logical_record;
        section.recovered_record_continuation = true;
        section.source_rows = paragraph.source_rows;
        section.label_source_rows = paragraph.source_rows;
        section.paragraphs.push_back(std::move(paragraph));
        remove_section_rows(entry.sections.front(), section.source_rows);
        entry.sections.push_back(std::move(section));
      }
    }
    if (entry.sections.size() != 2 ||
        entry.sections[0].kind != MessageSectionKind::meaning ||
        entry.sections[1].kind != MessageSectionKind::action)
      return fail(
          "message entry lacks ordered Meaning/Action sections: " + entry.id +
          " count=" + std::to_string(entry.sections.size()) +
          (entry.sections.empty() ? std::string{}
                                  : " first=" + std::string(section_name(
                                                    entry.sections[0].kind))));

    // LayoutIR deliberately rejects bytes that cannot be justified as a
    // physical display row. Message semantics must not reject those bytes in
    // turn: conserve only the still-opaque cells within each owned segment,
    // with their exact token/byte provenance. This is intentionally narrower
    // than replaying the segment payload, which would duplicate cells already
    // represented by semantic rows.
    const SegmentKey meaning_coordinate{entry.sections[0].logical_record,
                                        entry.sections[0].segment_index};
    const SegmentKey action_coordinate{entry.sections[1].logical_record,
                                       entry.sections[1].segment_index};
    for (const auto &[coordinate, owner] : entry_by_segment) {
      if (owner != entry_index)
        continue;
      const auto *record = find_record(records, coordinate.first);
      if (record == nullptr ||
          coordinate.second >= record->control_segments.size())
        continue;
      const auto &segment = record->control_segments[coordinate.second];
      const auto record_continuation =
          segment.kind == BookControlKind::unknown &&
          segment.segment_index == 0 &&
          std::any_of(std::next(record->control_segments.begin()),
                      record->control_segments.end(), [](const auto &item) {
                        return item.kind == BookControlKind::message_start;
                      });
      const auto structural_payload =
          segment.kind == BookControlKind::structural &&
          segment.payload_range.begin < segment.payload_range.end;
      if (segment.kind != BookControlKind::text &&
          segment.kind != BookControlKind::font && !record_continuation &&
          !structural_payload)
        continue;
      std::vector<OpaqueSegmentFragment> fragments;
      if (record_continuation) {
        DocumentSourceSliceIR source;
        auto text = complete_segment_text(*record, segment, &source);
        // A lone unknown word before SRMSG is the fixed row's terminal marker,
        // not flowing prose. Real record continuations contain at least one
        // lexical boundary (including short tails such as "and reopened.").
        if (text.find(' ') != std::string::npos)
          fragments.push_back({std::move(text), source});
      } else {
        fragments =
            opaque_segment_fragments(*record, ownership_index, segment);
      }
      const auto indexed_rows = rows_by_segment.find(coordinate);
      static const std::vector<SegmentRow> no_rows;
      const auto &segment_rows =
          indexed_rows == rows_by_segment.end() ? no_rows : indexed_rows->second;
      for (auto &fragment : fragments) {
        std::optional<MessageSourceRowIR> preceding_row;
        auto preceding_end = std::size_t{};
        auto next_begin = std::numeric_limits<std::size_t>::max();
        for (const auto &[source_row, token_begin, token_end] : segment_rows) {
          if (token_end <= fragment.source.token_begin &&
              (!preceding_row || token_end > preceding_end)) {
            preceding_row = source_row;
            preceding_end = token_end;
          }
          if (token_begin >= fragment.source.token_end)
            next_begin = std::min(next_begin, token_begin);
        }
        const auto attach_to_paragraph = [&](MessageParagraphIR &paragraph) {
          if (!preceding_row || fragment.source.token_end > next_begin)
            return false;
          if (std::find(paragraph.source_rows.begin(),
                        paragraph.source_rows.end(), *preceding_row) ==
              paragraph.source_rows.end())
            return false;
          append_text(paragraph.text, fragment.text);
          paragraph.source_slices.push_back(fragment.source);
          const auto semantic = std::find_if(
              paragraph.semantic_rows.begin(), paragraph.semantic_rows.end(),
              [&](const auto &row) { return row.source_row == *preceding_row; });
          if (semantic != paragraph.semantic_rows.end()) {
            append_text(semantic->text, fragment.text);
            semantic->trailing_source_slices.push_back(fragment.source);
          }
          return true;
        };
        const auto attach_to_preceding_row = [&](auto &paragraphs) {
          return std::any_of(paragraphs.begin(), paragraphs.end(),
                             attach_to_paragraph);
        };
        if (coordinate < meaning_coordinate) {
          if (attach_to_preceding_row(entry.headline_continuations) ||
              attach_to_paragraph(entry.headline))
            continue;
        } else if (coordinate < action_coordinate) {
          if (attach_to_preceding_row(entry.sections[0].paragraphs))
            continue;
        } else if (attach_to_preceding_row(entry.sections[1].paragraphs)) {
          continue;
        }
        MessageParagraphIR recovered;
        recovered.text = std::move(fragment.text);
        recovered.recovered_unformatted_segment = true;
        if (recovered.text.empty())
          continue;
        recovered.source_slices.push_back(fragment.source);
        auto append_if_new = [&](auto &paragraphs) {
          const auto already_present = std::any_of(
              paragraphs.begin(), paragraphs.end(), [&](const auto &paragraph) {
                return paragraph.text.find(recovered.text) != std::string::npos;
              });
          if (!already_present)
            paragraphs.push_back(std::move(recovered));
        };
        if (coordinate < meaning_coordinate) {
          append_if_new(entry.headline_continuations);
        } else if (coordinate < action_coordinate) {
          recovered.text = section_payload(MessageSectionKind::meaning,
                                           std::move(recovered.text));
          if (!recovered.text.empty())
            append_if_new(entry.sections[0].paragraphs);
        } else {
          recovered.text = section_payload(MessageSectionKind::action,
                                           std::move(recovered.text));
          if (!recovered.text.empty())
            append_if_new(entry.sections[1].paragraphs);
        }
      }
    }
    const auto source_order = [&layout](const auto &left, const auto &right) {
      return first_source_coordinate(layout, left) <
             first_source_coordinate(layout, right);
    };
    std::stable_sort(entry.headline_continuations.begin(),
                     entry.headline_continuations.end(), source_order);
    for (auto &section : entry.sections)
      std::stable_sort(section.paragraphs.begin(), section.paragraphs.end(),
                       source_order);
    for (auto &section : entry.sections)
      section.paragraphs.erase(std::remove_if(section.paragraphs.begin(),
                                              section.paragraphs.end(),
                                              [](const auto &paragraph) {
                                                return paragraph.text.empty();
                                              }),
                               section.paragraphs.end());

    {
      DocumentSourceSliceIR meaning_tail_source;
      auto meaning_tail = opaque_text_before_segment(
          records, ownership_index, entry.sections[1].logical_record,
          entry.sections[1].segment_index, &meaning_tail_source);
      meaning_tail =
          section_payload(MessageSectionKind::meaning, std::move(meaning_tail));
      meaning_tail = clean_boundary_continuation(std::move(meaning_tail));
      const auto already_present = std::any_of(
          entry.sections[0].paragraphs.begin(),
          entry.sections[0].paragraphs.end(), [&](const auto &paragraph) {
            return !meaning_tail.empty() &&
                   paragraph.text.find(meaning_tail) != std::string::npos;
          });
      if (!meaning_tail.empty() && !already_present) {
        MessageParagraphIR recovered;
        recovered.text = std::move(meaning_tail);
        recovered.recovered_unformatted_segment = true;
        recovered.source_slices.push_back(meaning_tail_source);
        entry.sections[0].paragraphs.push_back(std::move(recovered));
        std::stable_sort(entry.sections[0].paragraphs.begin(),
                         entry.sections[0].paragraphs.end(), source_order);
      }
    }

    if (entry_index + 1 < catalog.entries.size()) {
      const auto &next = catalog.entries[entry_index + 1];
      DocumentSourceSliceIR boundary_source;
      auto boundary_text = opaque_text_before_segment(
          records, ownership_index, next.logical_record, next.segment_index,
          &boundary_source);
      boundary_text =
          section_payload(MessageSectionKind::action, std::move(boundary_text));
      boundary_text = clean_boundary_continuation(std::move(boundary_text));
      const auto already_present = std::any_of(
          entry.sections[1].paragraphs.begin(),
          entry.sections[1].paragraphs.end(), [&](const auto &paragraph) {
            return !boundary_text.empty() &&
                   paragraph.text.find(boundary_text) != std::string::npos;
          });
      if (!boundary_text.empty() && !already_present) {
        MessageParagraphIR recovered;
        recovered.text = std::move(boundary_text);
        recovered.recovered_unformatted_segment = true;
        recovered.source_slices.push_back(boundary_source);
        entry.sections[1].paragraphs.push_back(std::move(recovered));
        std::stable_sort(entry.sections[1].paragraphs.begin(),
                         entry.sections[1].paragraphs.end(), source_order);
      }
    }

    for (auto &section : entry.sections) {
      std::string row_text;
      for (const auto &paragraph : section.paragraphs)
        if (!paragraph.recovered_unformatted_segment)
          append_text(row_text, paragraph.text);
      section.paragraphs.erase(
          std::remove_if(section.paragraphs.begin(), section.paragraphs.end(),
                         [&](const auto &paragraph) {
                           return paragraph.recovered_unformatted_segment &&
                                  paragraph.text.size() > 3 &&
                                  row_text.find(paragraph.text) !=
                                      std::string::npos;
                         }),
          section.paragraphs.end());
    }

    if (entry.headline.text.find(entry.id) == std::string::npos) {
      const auto headline = std::find_if(
          entry.headline_continuations.begin(),
          entry.headline_continuations.end(), [&](const auto &paragraph) {
            return paragraph.text.find(entry.id) != std::string::npos;
          });
      const auto fallback =
          headline != entry.headline_continuations.end()
              ? headline
              : std::find_if(entry.headline_continuations.begin(),
                             entry.headline_continuations.end(),
                             [](const auto &paragraph) {
                               return !paragraph.text.empty();
                             });
      if (fallback != entry.headline_continuations.end()) {
        entry.suppressed_source_rows.insert(entry.suppressed_source_rows.end(),
                                            entry.headline.source_rows.begin(),
                                            entry.headline.source_rows.end());
        for (auto preceding = entry.headline_continuations.begin();
             preceding != fallback; ++preceding)
          entry.suppressed_source_rows.insert(
              entry.suppressed_source_rows.end(),
              preceding->source_rows.begin(), preceding->source_rows.end());
        entry.headline = std::move(*fallback);
        entry.headline_continuations.erase(entry.headline_continuations.begin(),
                                           std::next(fallback));
      }
    }
    if (entry.headline.text.empty()) {
      const auto *source = find_record(records, entry.logical_record);
      if (source != nullptr) {
        const auto font = std::find_if(
            source->control_segments.begin() +
                static_cast<std::ptrdiff_t>(entry.segment_index + 1),
            source->control_segments.end(), [](const auto &segment) {
              return segment.kind == BookControlKind::font;
            });
        if (font != source->control_segments.end()) {
          entry.headline.text = entry.id;
          const auto payload = collapse_ascii_whitespace(
              trim_ascii(range_text(*source, font->payload_range)));
          append_text(entry.headline.text, payload);
          entry.headline.source_segments.push_back(
              {source->logical_record, font->segment_index});
        }
      }
    }
    if (entry.headline.text.empty() ||
        (entry.source_rows.empty() && entry.headline.source_segments.empty()))
      return fail("message entry lacks a source-proven headline: " + entry.id);
    if (entry.sections.size() == 2 &&
        entry.sections[0].kind == MessageSectionKind::meaning &&
        entry.sections[1].kind == MessageSectionKind::action &&
        entry.sections[0].paragraphs.size() == 1 &&
        entry.sections[0].paragraphs.front().text.empty()) {
      auto paragraph = paragraph_before_segment(
          records, layout, ownership_index, entry.sections[1].logical_record,
          entry.sections[1].segment_index);
      if (paragraph.text.empty()) {
        paragraph.text = opaque_text_before_segment(
            records, ownership_index, entry.sections[1].logical_record,
            entry.sections[1].segment_index);
        if (!paragraph.text.empty())
          paragraph.source_segments.push_back(
              {entry.sections[1].logical_record,
               entry.sections[1].segment_index - 1});
      }
      paragraph.text = section_payload(MessageSectionKind::meaning,
                                       std::move(paragraph.text));
      if (!paragraph.text.empty()) {
        remove_body_rows(entry, paragraph.source_rows);
        entry.sections[0].paragraphs.front() = paragraph;
        entry.sections[0].source_rows = paragraph.source_rows;
        entry.sections[0].recovered_record_continuation = true;
        remove_section_rows(entry.sections[1], paragraph.source_rows);
      }
    }
    for (auto &section : entry.sections) {
      if (section.kind == MessageSectionKind::action &&
          section.paragraphs.size() == 1 &&
          section.paragraphs.front().text.empty()) {
        const auto continuation = std::find_if(
            records.begin(), records.end(), [&](const auto &record) {
              return record.logical_record > section.logical_record;
            });
        if (continuation != records.end()) {
          const auto boundary = std::find_if(
              continuation->control_segments.begin(),
              continuation->control_segments.end(), [](const auto &segment) {
                return segment.kind == BookControlKind::message_start;
              });
          if (boundary != continuation->control_segments.end() &&
              boundary->segment_index == 1 &&
              continuation->control_segments.front().kind ==
                  BookControlKind::text) {
            auto &paragraph = section.paragraphs.front();
            paragraph.text = collapse_ascii_whitespace(trim_ascii(range_text(
                *continuation,
                continuation->control_segments.front().payload_range)));
            if (!paragraph.text.empty()) {
              paragraph.source_segments.push_back(
                  {continuation->logical_record, 0});
              section.recovered_record_continuation = true;
            }
          }
        }
      }
      if (section.paragraphs.empty() ||
          section.paragraphs.front().text.empty() ||
          (section.source_rows.empty() &&
           section.paragraphs.front().source_segments.empty() &&
           section.paragraphs.front().source_slices.empty())) {
        return fail("message section lacks source-proven text: " + entry.id +
                    " " + section_name(section.kind));
      }
    }
  }
  if (error != nullptr)
    error->clear();
  return catalog;
}

bool verify_message_catalog_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const MessageCatalogIR &catalog, std::string *error) {
  const auto fail = [&](const std::string &message) {
    if (error != nullptr)
      *error = message;
    return false;
  };
  const auto canonical = extract_message_catalog_ir(records, layout, ownership);
  if (!canonical)
    return fail("source does not admit a canonical numeric message catalog");
  if (!same_message_catalog_ir(*canonical, catalog))
    return fail("message catalog differs from canonical lowering");
  if (error != nullptr)
    error->clear();
  return true;
}

bool same_message_catalog_ir(const MessageCatalogIR &left,
                             const MessageCatalogIR &right) {
  return left.entries.size() == right.entries.size() &&
         std::equal(left.entries.begin(), left.entries.end(),
                    right.entries.begin(), entry_equal);
}

std::string format_message_catalog_ir(const MessageCatalogIR &catalog) {
  std::ostringstream out;
  out << "message_catalog entries=" << catalog.entries.size() << '\n';
  for (const auto &entry : catalog.entries) {
    out << "message id='" << entry.id << "' source=" << entry.logical_record
        << ':' << entry.segment_index << " headline='" << entry.headline.text
        << "' rows=" << entry.source_rows.size()
        << " suppressed=" << entry.suppressed_source_rows.size();
    for (const auto &section : entry.sections) {
      out << ' ' << section_name(section.kind) << '=' << section.run << ':'
          << section.row << '@' << section.logical_record << ':'
          << section.segment_index;
      if (section.recovered_record_continuation)
        out << "(continuation)";
      out << " text='";
      for (std::size_t paragraph = 0; paragraph < section.paragraphs.size();
           ++paragraph) {
        if (paragraph != 0)
          out << " | ";
        out << section.paragraphs[paragraph].text;
      }
      out << '\'';
    }
    out << '\n';
  }
  return out.str();
}

} // namespace geist::detail
