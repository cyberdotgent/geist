#include "geist/detail/ir/message_ir.hpp"

#include "geist/detail/core/internal.hpp"

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

bool starts_once_with_message_id(std::string_view text, std::string_view id) {
  if (id.empty() || text.size() < id.size() || text.substr(0, id.size()) != id ||
      (text.size() != id.size() &&
       std::isspace(static_cast<unsigned char>(text[id.size()])) == 0))
    return false;
  auto next = id.size();
  while (next < text.size() &&
         std::isspace(static_cast<unsigned char>(text[next])) != 0)
    ++next;
  return text.substr(next, id.size()) != id ||
         (next + id.size() < text.size() &&
          std::isspace(static_cast<unsigned char>(text[next + id.size()])) == 0);
}

bool balanced_angle_placeholders(std::string_view text) {
  auto depth = std::size_t{};
  for (const auto character : text) {
    if (character == '<')
      ++depth;
    else if (character == '>') {
      if (depth == 0)
        return false;
      --depth;
    }
  }
  return depth == 0;
}

const DecodedLogicalRecordSource *
find_record(const std::vector<DecodedLogicalRecordSource> &records,
            std::uint32_t logical_record);

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
         left.boundary_index == right.boundary_index &&
         left.label_source_rows == right.label_source_rows &&
         source_slices_equal(left.label_source_slices,
                             right.label_source_slices) &&
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
                             a.suppressed_layout_tokens ==
                                 b.suppressed_layout_tokens &&
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
      left.headline.suppressed_layout_tokens !=
          right.headline.suppressed_layout_tokens ||
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
        left.headline_continuations[index].suppressed_layout_tokens !=
            right.headline_continuations[index].suppressed_layout_tokens ||
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

const char *boundary_shape_name(MessageSectionBoundaryShapeIR shape) {
  switch (shape) {
  case MessageSectionBoundaryShapeIR::normal_row: return "normal_row";
  case MessageSectionBoundaryShapeIR::record_prefix: return "record_prefix";
  case MessageSectionBoundaryShapeIR::pre_message_start:
    return "pre_message_start";
  }
  return "invalid";
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
using SegmentKey = std::pair<std::uint32_t, std::size_t>;
using SegmentRow =
    std::tuple<MessageSourceRowIR, std::size_t, std::size_t>;

bool structural_padding_token(const LogicalTokenIR &token);
std::string decoded_token_text(const LogicalTokenIR &token);

bool token_text_is(const LogicalTokenIR &token, std::string_view expected) {
  if (token.decoded_words.size() != expected.size())
    return false;
  for (std::size_t index = 0; index < expected.size(); ++index)
    if (token.decoded_words[index] !=
        static_cast<unsigned char>(expected[index]))
      return false;
  return true;
}

struct MessageOwnershipIndex {
  std::map<MessageSourceRowIR, std::vector<const OwnedSourceCellIR *>> rows;
  std::map<MessageSourceRowIR, std::vector<const PositionedRowCellIR *>>
      positioned_rows;
  std::map<MessageCellKey, const OwnedSourceCellIR *> cells;
  std::map<std::uint32_t, const DecodedLogicalRecordSource *> records;
  std::map<std::uint32_t, std::vector<std::vector<std::size_t>>> token_outputs;
};

struct SectionLabelMatch {
  MessageSectionKind kind = MessageSectionKind::meaning;
  std::size_t token = 0;
};

std::optional<SectionLabelMatch> exact_section_label(
    const DecodedLogicalRecordSource &record, const ControlSegmentIR &segment,
    const MessageOwnershipIndex &ownership) {
  const auto range =
      decoded_byte_range_to_word_range(record.assembled, segment.payload_range);
  std::optional<std::size_t> previous;
  for (std::size_t output = range.begin;
       output < range.end && output < record.assembled.sources.size(); ++output) {
    const auto &source = record.assembled.sources[output];
    if (source.kind != LogicalWordSourceKind::token_word ||
        source.token_index >= record.ir.tokens.size() ||
        previous == source.token_index)
      continue;
    previous = source.token_index;
    const auto &token = record.ir.tokens[source.token_index];
    if (structural_padding_token(token))
      continue;
    const auto is_layout = [&] {
      for (std::size_t word = 0; word < token.decoded_words.size(); ++word) {
        const auto cell = ownership.cells.find(
            {record.logical_record, source.token_index, word});
        if (cell != ownership.cells.end() &&
            (cell->second->disposition == SourceDisposition::opaque ||
             cell->second->disposition ==
                 SourceDisposition::visible_content))
          return false;
      }
      return true;
    };
    if (token_text_is(token, "Meaning"))
      return SectionLabelMatch{MessageSectionKind::meaning,
                               source.token_index};
    if (token_text_is(token, "Action"))
      return SectionLabelMatch{MessageSectionKind::action,
                               source.token_index};
    if (!is_layout())
      return std::nullopt;
  }
  return std::nullopt;
}

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
  for (const auto &cell : ownership.row_cells)
    result.positioned_rows[{cell.run, cell.row_index}].push_back(&cell);
  return result;
}

std::optional<MessageSectionBoundaryIR> section_boundary_for_segment(
    const DecodedLogicalRecordSource &record, const ControlSegmentIR &segment,
    const MessageOwnershipIndex &ownership, std::size_t owner_entry,
    const std::map<SegmentKey, std::vector<SegmentRow>> &rows_by_segment,
    const std::set<SegmentKey> &later_continuation) {
  const auto range =
      decoded_byte_range_to_word_range(record.assembled, segment.payload_range);
  std::vector<std::size_t> tokens;
  for (std::size_t output = range.begin;
       output < range.end && output < record.assembled.sources.size(); ++output) {
    const auto &source = record.assembled.sources[output];
    if (source.kind != LogicalWordSourceKind::token_word ||
        source.token_index >= record.ir.tokens.size() ||
        (!tokens.empty() && tokens.back() == source.token_index))
      continue;
    tokens.push_back(source.token_index);
  }
  const auto lexical = [&](std::size_t token) {
    return token < record.ir.tokens.size() &&
           !structural_padding_token(record.ir.tokens[token]);
  };
  const auto match = exact_section_label(record, segment, ownership);
  if (!match)
    return std::nullopt;
  const auto first = std::find(tokens.begin(), tokens.end(), match->token);
  if (first == tokens.end())
    return std::nullopt;
  const auto kind = match->kind;

  auto label_last = *first;
  auto payload = std::next(first);
  bool saw_colon = false;
  for (; payload != tokens.end(); ++payload) {
    if (!lexical(*payload))
      continue;
    if (!saw_colon && token_text_is(record.ir.tokens[*payload], ":")) {
      saw_colon = true;
      label_last = *payload;
      continue;
    }
    break;
  }
  const auto &label_begin_token = record.ir.tokens[*first];
  const auto &label_end_token = record.ir.tokens[label_last];
  MessageSectionBoundaryIR boundary;
  boundary.kind = kind;
  boundary.owner_entry = owner_entry;
  boundary.shape = MessageSectionBoundaryShapeIR::normal_row;
  boundary.label_source = {
      record.logical_record, segment.segment_index, *first, label_last + 1,
      label_begin_token.byte_range.begin, label_end_token.byte_range.end};
  const auto rows =
      rows_by_segment.find({record.logical_record, segment.segment_index});
  if (rows != rows_by_segment.end())
    for (const auto &[source_row, token_begin, token_end] : rows->second)
      if (token_begin < label_last + 1 && *first < token_end) {
        boundary.label_source_row = source_row;
        break;
      }
  if (!boundary.label_source_row && rows != rows_by_segment.end()) {
    auto nearest = std::numeric_limits<std::size_t>::max();
    for (const auto &[source_row, token_begin, token_end] : rows->second)
      if (token_begin >= label_last + 1 && token_begin < nearest) {
        nearest = token_begin;
        boundary.label_source_row = source_row;
      }
  }
  const auto visible_label = std::any_of(
      ownership.cells.lower_bound(
          {record.logical_record, *first, std::size_t{0}}),
      ownership.cells.upper_bound(
          {record.logical_record, *first,
           std::numeric_limits<std::size_t>::max()}),
      [](const auto &item) {
        return item.second->disposition == SourceDisposition::visible_content;
      });
  const auto continuation_label = later_continuation.count(
                                      {record.logical_record,
                                       segment.segment_index}) != 0;
  if (segment.kind == BookControlKind::text && segment.segment_index == 0 &&
      !visible_label && !continuation_label) {
    const auto next = std::next(
        record.control_segments.begin(),
        static_cast<std::ptrdiff_t>(segment.segment_index + 1));
    boundary.shape =
        next != record.control_segments.end() &&
                next->kind == BookControlKind::message_start
            ? MessageSectionBoundaryShapeIR::pre_message_start
            : MessageSectionBoundaryShapeIR::record_prefix;
  }
  if (payload != tokens.end()) {
    const auto last = std::find_if(tokens.rbegin(), tokens.rend(), lexical);
    if (last != tokens.rend() && *last >= *payload) {
      const auto &payload_begin_token = record.ir.tokens[*payload];
      const auto &payload_end_token = record.ir.tokens[*last];
      boundary.payload_source_slices.push_back(
          {record.logical_record, segment.segment_index, *payload, *last + 1,
           payload_begin_token.byte_range.begin,
           payload_end_token.byte_range.end});
      if (rows != rows_by_segment.end())
        for (const auto &[source_row, token_begin, token_end] : rows->second)
          if (token_begin < *last + 1 && *payload < token_end)
            boundary.payload_source_rows.push_back(source_row);
      if (!boundary.label_source_row &&
          !boundary.payload_source_rows.empty())
        boundary.label_source_row = boundary.payload_source_rows.front();
    }
  }
  return boundary;
}

std::string source_slice_text(const DecodedLogicalRecordSource &record,
                              const DocumentSourceSliceIR &slice) {
  if (slice.token_begin >= slice.token_end ||
      slice.token_end > record.tokens.size())
    return {};
  std::vector<TokenWords> tokens(
      record.tokens.begin() + static_cast<std::ptrdiff_t>(slice.token_begin),
      record.tokens.begin() + static_cast<std::ptrdiff_t>(slice.token_end));
  return collapse_ascii_whitespace(
      trim_ascii(token_words_to_ascii(assemble_logical_record(tokens))));
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
      // A row's token range can cross a display line boundary, so the line's
      // own length byte lands inside it. The framing says it is structure;
      // its dictionary spelling ("agent", "message", "an") is never text.
      if (cell->field_role == SourceFieldRole::supplemental)
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
          decoded == "<" || decoded == "-" ||
          (decoded == ">" &&
           std::count(row.visible_text.begin(), row.visible_text.end(), '<') <
               std::count(row.visible_text.begin(), row.visible_text.end(),
                          '>'));
      // A cell the record's own framing places inside a display line is drawn
      // text: never suppress it, whatever the dictionary spells. The length
      // bytes are already gone from `visible_tokens`, so this envelope only
      // ever applies to a record whose payload does not tile into display
      // lines, where the framing has no answer; delimiter balancing prevents
      // dropping source punctuation there.
      if (source_field_role(*record, token) != SourceFieldRole::positioned &&
          source.encoded.width == 1 && source.encoded.value >= 19 &&
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
  return (begin < token.decoded_words.size() ||
          (token.has_spacing_control && begin == token.decoded_words.size())) &&
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
    const MessageOwnershipIndex &ownership, const PhysicalRowIR &row,
    DocumentSourceSliceIR *source_slice = nullptr) {
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
  std::optional<std::size_t> first_token;
  std::optional<std::size_t> last_token;
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
    if (!first_token)
      first_token = source.token_index;
    last_token = source.token_index;
    result.push_back(static_cast<char>(found->second->word));
  }
  result = collapse_ascii_whitespace(trim_ascii(std::move(result)));
  if (!result.empty() && source_slice != nullptr && first_token && last_token) {
    const auto &first = record->ir.tokens[*first_token];
    const auto &last = record->ir.tokens[*last_token];
    *source_slice = {row.logical_record,     row.segment_index,
                     *first_token,           *last_token + 1,
                     first.byte_range.begin, last.byte_range.end};
  }
  return result;
}

std::string
opaque_segment_suffix(const std::vector<DecodedLogicalRecordSource> &records,
                      const MessageOwnershipIndex &ownership,
                      const PhysicalRowIR &row,
                      DocumentSourceSliceIR *source_slice = nullptr) {
  const auto *record = find_record(records, row.logical_record);
  if (record == nullptr || row.segment_index >= record->control_segments.size())
    return {};
  const auto &segment = record->control_segments[row.segment_index];
  const auto range = decoded_byte_range_to_word_range(record->assembled,
                                                      segment.payload_range);
  std::string result;
  std::optional<std::size_t> first_token;
  std::optional<std::size_t> last_token;
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
    if (!first_token)
      first_token = source.token_index;
    last_token = source.token_index;
    result.push_back(static_cast<char>(found->second->word));
  }
  result = collapse_ascii_whitespace(trim_ascii(std::move(result)));
  if (!result.empty() && source_slice != nullptr && first_token && last_token) {
    const auto &first = record->ir.tokens[*first_token];
    const auto &last = record->ir.tokens[*last_token];
    *source_slice = {row.logical_record,     row.segment_index,
                     *first_token,           *last_token + 1,
                     first.byte_range.begin, last.byte_range.end};
  }
  return result;
}

struct OpaqueSegmentFragment {
  std::string text;
  DocumentSourceSliceIR source;
  std::vector<MessageTerminalLayoutTokenIR> suppressed_layout_tokens;
};

std::string decoded_token_text(const LogicalTokenIR &token) {
  std::string text;
  for (const auto word : token.decoded_words)
    if (word <= 0xff)
      text.push_back(static_cast<char>(word));
  return text;
}

MessageTerminalLayoutTokenIR
layout_token_evidence(const DecodedLogicalRecordSource &record,
                      std::size_t token_index) {
  const auto &token = record.ir.tokens[token_index];
  return {record.logical_record, token_index, token.encoded, token.byte_range,
          decoded_token_text(token)};
}

bool closes_unmatched_delimiter(std::string_view prefix,
                                std::string_view closing) {
  if (closing.size() != 1)
    return false;
  const auto opener = [closing]() -> char {
    switch (closing.front()) {
    case '>':
      return '<';
    case ')':
      return '(';
    case ']':
      return '[';
    case '}':
      return '{';
    default:
      return '\0';
    }
  }();
  if (opener == '\0')
    return false;
  auto depth = std::size_t{};
  for (const auto ch : prefix) {
    if (ch == opener)
      ++depth;
    else if (ch == closing.front() && depth != 0)
      --depth;
  }
  return depth != 0;
}

bool opens_unmatched_delimiter(std::string_view opening,
                               std::string_view suffix) {
  if (opening.size() != 1)
    return false;
  const auto closing = [opening]() -> char {
    switch (opening.front()) {
    case '<': return '>';
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    default: return '\0';
    }
  }();
  if (closing == '\0')
    return false;
  auto depth = std::size_t{};
  for (const auto character : suffix) {
    if (character == opening.front())
      ++depth;
    else if (character == closing) {
      if (depth == 0)
        return true;
      --depth;
    }
  }
  return false;
}

std::vector<OpaqueSegmentFragment>
opaque_segment_fragments(const DecodedLogicalRecordSource &record,
                         const MessageOwnershipIndex &ownership,
                         const ControlSegmentIR &segment) {
  const auto range =
      decoded_byte_range_to_word_range(record.assembled, segment.payload_range);
  auto terminal_layout_token = std::numeric_limits<std::size_t>::max();
  auto leading_layout_token = std::numeric_limits<std::size_t>::max();
  for (std::size_t output = range.begin;
       output < range.end && output < record.assembled.sources.size();
       ++output) {
    const auto &source = record.assembled.sources[output];
    const auto found = ownership.cells.find(
        {record.logical_record, source.token_index, source.word_index});
    if (found != ownership.cells.end() &&
        found->second->disposition == SourceDisposition::opaque)
      terminal_layout_token = source.token_index;
    if (leading_layout_token == std::numeric_limits<std::size_t>::max() &&
        found != ownership.cells.end() &&
        found->second->disposition == SourceDisposition::opaque)
      leading_layout_token = source.token_index;
  }
  if (terminal_layout_token < record.ir.tokens.size()) {
    const auto &token = record.ir.tokens[terminal_layout_token];
    // The record's display-line framing decides this: a supplemental slot is
    // the line's length byte and is never text, a positioned one is drawn
    // text the fragment must keep. Only an unframed record falls back to the
    // observed compact alphabet, with delimiter balancing so that source
    // punctuation is not dropped in the meantime.
    const auto field_role = source_field_role(record, terminal_layout_token);
    if (field_role == SourceFieldRole::positioned)
      terminal_layout_token = std::numeric_limits<std::size_t>::max();
    else if (field_role == SourceFieldRole::undecided &&
             (token.encoded.width != 1 || token.encoded.value < 19 ||
              token.encoded.value > 43))
      terminal_layout_token = std::numeric_limits<std::size_t>::max();
    else if (field_role == SourceFieldRole::undecided) {
      std::string preceding_text;
      for (std::size_t output = range.begin;
           output < range.end && output < record.assembled.sources.size();
           ++output) {
        const auto &source = record.assembled.sources[output];
        if (source.token_index == terminal_layout_token)
          break;
        const auto found = ownership.cells.find(
            {record.logical_record, source.token_index, source.word_index});
        if (found != ownership.cells.end() &&
            found->second->disposition == SourceDisposition::opaque &&
            found->second->word <= 0xff && !unmapped_cell(record, *found->second))
          preceding_text.push_back(static_cast<char>(found->second->word));
      }
      if (closes_unmatched_delimiter(preceding_text,
                                     decoded_token_text(token)))
        terminal_layout_token = std::numeric_limits<std::size_t>::max();
    }
  }
  if (leading_layout_token < record.ir.tokens.size()) {
    const auto &token = record.ir.tokens[leading_layout_token];
    // As above: the framing decides, and only an unframed record falls back
    // to the observed compact envelope plus the padding that follows it.
    auto followed_by_padding = false;
    for (const auto source_token : segment.source_tokens) {
      if (source_token <= leading_layout_token ||
          source_token >= record.ir.tokens.size())
        continue;
      followed_by_padding = structural_padding_token(record.ir.tokens[source_token]);
      break;
    }
    const auto field_role = source_field_role(record, leading_layout_token);
    if (field_role == SourceFieldRole::positioned)
      leading_layout_token = std::numeric_limits<std::size_t>::max();
    else if (field_role == SourceFieldRole::undecided &&
             (token.encoded.width != 1 || token.encoded.value < 19 ||
              token.encoded.value > 43 || !followed_by_padding))
      leading_layout_token = std::numeric_limits<std::size_t>::max();
    else if (field_role == SourceFieldRole::undecided) {
      std::string following_text;
      for (std::size_t output = range.begin;
           output < range.end && output < record.assembled.sources.size();
           ++output) {
        const auto &source = record.assembled.sources[output];
        if (source.token_index <= leading_layout_token)
          continue;
        const auto found = ownership.cells.find(
            {record.logical_record, source.token_index, source.word_index});
        if (found != ownership.cells.end() &&
            found->second->disposition == SourceDisposition::opaque &&
            found->second->word <= 0xff && !unmapped_cell(record, *found->second))
          following_text.push_back(static_cast<char>(found->second->word));
      }
      if (opens_unmatched_delimiter(decoded_token_text(token), following_text))
        leading_layout_token = std::numeric_limits<std::size_t>::max();
    }
  }
  std::vector<OpaqueSegmentFragment> result;
  std::string text;
  std::vector<MessageTerminalLayoutTokenIR> suppressed_layout_tokens;
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
      result.push_back({
          std::move(text),
          {record.logical_record, segment.segment_index, *first_token,
           *last_token + 1, first.byte_range.begin, last.byte_range.end},
          std::move(suppressed_layout_tokens)});
    }
    text.clear();
    first_token.reset();
    last_token.reset();
    suppressed_layout_tokens.clear();
  };
  for (std::size_t output = range.begin;
       output < range.end && output < record.assembled.sources.size();
       ++output) {
    const auto &source = record.assembled.sources[output];
    if (source.token_index >= record.ir.tokens.size()) {
      if (!text.empty())
        finish();
      continue;
    }
    const auto is_leading_layout = leading_layout_token == source.token_index;
    const auto is_terminal_layout = terminal_layout_token == source.token_index;
    if (is_leading_layout || is_terminal_layout) {
      if (source.word_index == 0)
        suppressed_layout_tokens.push_back(
            layout_token_evidence(record, source.token_index));
      if (!text.empty() && is_terminal_layout)
        finish();
      continue;
    }
    if (
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

bool has_positioned_boundary(const MessageOwnershipIndex &ownership,
                             const MessageSourceRowIR &source,
                             const PhysicalRowIR &row) {
  if (!row.marker)
    return false;
  const auto positioned = ownership.positioned_rows.find(source);
  return positioned != ownership.positioned_rows.end() &&
         std::any_of(positioned->second.begin(), positioned->second.end(),
                     [&](const auto *cell) {
                       return cell->role == RowCellRole::boundary &&
                              !cell->display_column &&
                              cell->logical_record == row.logical_record &&
                              cell->token_index == row.marker->token_index;
                     });
}

// The field role the record's own display-line framing gives this row's
// marker slot. `supplemental` is the display line's length byte: structure,
// never display text, whatever word the dictionary spells for it.
// `positioned` is a word the framing draws inside a line, which the row
// merely happens to open on. `undecided` means the record's payload does not
// tile into display lines and the framing has no answer at all.
SourceFieldRole marker_field_role(const MessageOwnershipIndex &ownership,
                                  const MessageSourceRowIR &source,
                                  const PhysicalRowIR &row) {
  if (!row.marker)
    return SourceFieldRole::undecided;
  const auto positioned = ownership.positioned_rows.find(source);
  if (positioned == ownership.positioned_rows.end())
    return SourceFieldRole::undecided;
  for (const auto *cell : positioned->second) {
    if (cell->role == RowCellRole::boundary &&
        cell->logical_record == row.logical_record &&
        cell->token_index == row.marker->token_index)
      return cell->field_role;
  }
  return SourceFieldRole::undecided;
}

bool compact_fixed_row_candidate(const MessageOwnershipIndex &ownership,
                                 const DisplayRunIR &run,
                                 std::size_t row_index) {
  if (row_index >= run.rows.size())
    return false;
  const auto &row = run.rows[row_index];
  // TODO: The positioned ledger marks every marker slot as a geometry-only
  // boundary and cannot yet distinguish a fixed-field series from wrapped
  // prose ordinals (MSG2267 `1`..`5`) or the value-34 `and` collision between
  // MSG739 and MSG2108. Keep the observed compact alphabet and indentation as
  // series evidence until OwnershipIR carries a typed boundary purpose.
  return has_positioned_boundary(ownership, {run.id, row_index}, row) &&
         row.start == PhysicalRowStartKind::explicit_marker_slot &&
         !row.continues_previous_record && row.marker->encoded_width == 1 &&
         row.marker->encoded_value >= 19 && row.marker->encoded_value <= 43 &&
         row.native_origin >= 13;
}

bool has_fixed_row_context(const MessageOwnershipIndex &ownership,
                           const DisplayRunIR &run, std::size_t row_index) {
  if (!compact_fixed_row_candidate(ownership, run, row_index))
    return false;
  const auto &row = run.rows[row_index];
  const auto adjacent_candidate = [&](std::size_t candidate) {
    return candidate < run.rows.size() &&
           run.rows[candidate].logical_record == row.logical_record &&
           run.rows[candidate].segment_index == row.segment_index &&
           compact_fixed_row_candidate(ownership, run, candidate);
  };
  // One compact spelling is ambiguous.  A neighboring row with the same
  // mechanical envelope establishes a fixed-position row series without
  // consulting the marker's decoded word.
  if ((row_index != 0 && adjacent_candidate(row_index - 1)) ||
      adjacent_candidate(row_index + 1))
    return true;

  // A font control can host one isolated indented row between two explicit
  // sentence rows.  In that envelope the compact slot is the row delimiter;
  // an otherwise identical compact word in an unstyled text run remains
  // lexical.  This deliberately does not inspect decoded spelling.
  const auto sentence_row = [&](std::size_t candidate) {
    if (candidate >= run.rows.size())
      return false;
    const auto &neighbor = run.rows[candidate];
    return neighbor.logical_record == row.logical_record &&
           neighbor.segment_index == row.segment_index && neighbor.marker &&
           has_positioned_boundary(ownership, {run.id, candidate}, neighbor) &&
           neighbor.start == PhysicalRowStartKind::explicit_marker_slot &&
           neighbor.native_origin == 1 &&
           neighbor.marker->encoded_width == 1 &&
           neighbor.marker->encoded_value == 1;
  };
  return run.control_kind == BookControlKind::font && row_index != 0 &&
         sentence_row(row_index - 1) && sentence_row(row_index + 1);
}

// Punctuation that closes the text preceding it rather than opening the text
// that follows. Used both to classify a row marker and to join one back onto
// the words it terminates without inserting a space.
bool trailing_punctuation_marker(const std::string &text) {
  if (text.size() != 1)
    return false;
  switch (text.front()) {
  case '.':
  case ',':
  case ':':
  case ';':
  case '!':
  case '?':
  case ']':
  case '}':
    return true;
  default:
    return false;
  }
}

MessageMarkerDispositionIR marker_disposition(const DisplayRunIR &run,
                                              const PhysicalRowIR &row,
                                              const MessageOwnershipIndex &ownership,
                                              bool section_label,
                                              std::size_t row_index,
                                              bool has_opaque_prefix,
                                              bool initial_headline_row) {
  if (!row.marker)
    return MessageMarkerDispositionIR::absent;
  if (!has_positioned_boundary(ownership, {run.id, row_index}, row))
    return MessageMarkerDispositionIR::layout_artifact;
  if (section_label || initial_headline_row)
    return MessageMarkerDispositionIR::layout_artifact;
  const auto &marker = *row.marker;
  const auto single = marker.decoded_text.size() == 1
                          ? static_cast<unsigned char>(marker.decoded_text[0])
                          : 0;
  if (single == '-' || single == '<' || single == '>' || single == '/' ||
      single == '"' || single == '=' || single == '(' || single == ')' ||
      single == '[' || single == '{')
    return MessageMarkerDispositionIR::layout_artifact;
  if (trailing_punctuation_marker(marker.decoded_text)) {
    // A supplemental slot is the display line's length byte: the soft wrap
    // itself, not punctuation the record draws. The record's framing decides
    // that, not the encoded value; only an unframed record falls back to the
    // observed decoder sentinel.
    const auto field_role = marker_field_role(ownership, {run.id, row_index}, row);
    if (field_role == SourceFieldRole::supplemental)
      return MessageMarkerDispositionIR::layout_artifact;
    if (field_role == SourceFieldRole::undecided && marker.encoded_value == 4)
      return MessageMarkerDispositionIR::layout_artifact;
    // The marker closes whatever text precedes it. When this row carries an
    // opaque continuation prefix, that preceding text is the prefix in this
    // same row, not the previously emitted row, so the marker must travel
    // with the row instead of being appended to the paragraph so far.
    if (has_opaque_prefix)
      return MessageMarkerDispositionIR::opaque_continuation_suffix;
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
    // The record's own display-line framing decides this, never the marker's
    // spelling: a supplemental slot is the line's length byte -- structure,
    // whatever word the dictionary spells for it -- while a positioned slot
    // is a word the framing draws inside a line and the row merely opens on.
    switch (marker_field_role(ownership, {run.id, row_index}, row)) {
    case SourceFieldRole::supplemental:
      return MessageMarkerDispositionIR::layout_artifact;
    case SourceFieldRole::positioned:
      return MessageMarkerDispositionIR::lexical_prefix;
    case SourceFieldRole::undecided:
      break;
    }
    // The record's payload does not tile into display lines, so the framing
    // has no answer. Keep the observed compact envelope for that case rather
    // than guess: values 28..43 are the observed fixed-row marker alphabet
    // only at the first explicit marker slot of a new run.
    if (has_fixed_row_context(ownership, run, row_index))
      return MessageMarkerDispositionIR::layout_artifact;
    return MessageMarkerDispositionIR::lexical_prefix;
  }
  return MessageMarkerDispositionIR::layout_artifact;
}

MessageSemanticRowIR
semantic_row(const std::vector<DecodedLogicalRecordSource> &records,
             const MessageOwnershipIndex &ownership, const PhysicalRowIR &row,
             const DisplayRunIR &run, std::size_t row_index, bool section_label,
             bool suppress_terminal_layout_word = false,
             bool recover_segment_suffix = false,
             bool initial_headline_row = false) {
  MessageSemanticRowIR result;
  result.source_row = {run.id, row_index};
  auto visible = collapse_ascii_whitespace(trim_ascii(
      owned_row_text(ownership, row, row_index, suppress_terminal_layout_word,
                     &result.terminal_layout_token)));
  DocumentSourceSliceIR prefix_source;
  const auto prefix =
      opaque_continuation_prefix(records, ownership, row, &prefix_source);
  if (!prefix.empty())
    result.leading_source_slices.push_back(prefix_source);
  result.marker_disposition =
      marker_disposition(run, row, ownership, section_label, row_index,
                         !prefix.empty(), initial_headline_row);
  if (row.marker &&
      result.marker_disposition ==
          MessageMarkerDispositionIR::opaque_continuation_suffix) {
    const auto &marker_text = row.marker->decoded_text;
    visible = prefix +
              (trailing_punctuation_marker(marker_text) ? std::string{}
                                                        : std::string{" "}) +
              marker_text +
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
    DocumentSourceSliceIR suffix_source;
    const auto suffix =
        opaque_segment_suffix(records, ownership, row, &suffix_source);
    if (!suffix.empty()) {
      visible += (visible.empty() ? std::string{} : std::string{" "}) + suffix;
      result.trailing_source_slices.push_back(suffix_source);
    }
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

std::string typed_section_payload(const MessageSectionBoundaryIR &boundary,
                                  std::string text) {
  text = collapse_ascii_whitespace(trim_ascii(std::move(text)));
  const std::string label = boundary.kind == MessageSectionKind::meaning
                                ? "Meaning"
                                : "Action";
  // The label row is the row the typed boundary points at. When the boundary
  // recovered its label from a record continuation the label word is carried
  // by the boundary's own source slice and never reaches this row's visible
  // text; the row is then body text in full. Returning the text unchanged
  // keeps those words instead of discarding a row whose spelling did not
  // happen to repeat the label.
  if (text.size() < label.size() || text.compare(0, label.size(), label) != 0)
    return text;
  auto begin = label.size();
  if (begin < text.size() && text[begin] == ':')
    ++begin;
  return trim_ascii(text.substr(begin));
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
  for (const auto &run : layout.runs)
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index)
      if (run.rows[row_index].marker &&
          !has_positioned_boundary(ownership_index, {run.id, row_index},
                                   run.rows[row_index]))
        return fail("message row lacks positioned boundary provenance");

  std::map<SegmentKey, std::vector<SegmentRow>> rows_by_segment;
  std::set<SegmentKey> later_continuation;
  for (const auto &run : layout.runs)
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      rows_by_segment[{row.logical_record, row.segment_index}].push_back(
          {{run.id, row_index}, row.token_begin, row.token_end});
      if (row.start == PhysicalRowStartKind::record_continuation)
        for (std::size_t segment = 0; segment < row.segment_index; ++segment)
          later_continuation.emplace(row.logical_record, segment);
    }
  std::map<SegmentKey, std::size_t> entry_by_segment;
  // A record-terminal SRMSG with an empty payload has its payload (the
  // message separator row) physically carried by the next record's leading
  // text segment. That segment is message_start payload, not entry text.
  std::set<SegmentKey> message_start_overflow;
  MessageCatalogIR catalog;
  std::optional<std::size_t> active;
  bool saw_nonnumeric_message = false;
  bool pending_message_start_overflow = false;
  for (const auto &record : records) {
    if (pending_message_start_overflow && !record.control_segments.empty() &&
        record.control_segments.front().kind == BookControlKind::text &&
        record.control_segments.front().segment_index == 0)
      message_start_overflow.emplace(record.logical_record, 0);
    pending_message_start_overflow =
        !record.control_segments.empty() &&
        record.control_segments.back().kind == BookControlKind::message_start &&
        record.control_segments.back().payload_range.begin ==
            record.control_segments.back().payload_range.end;
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

  // Reject non-catalog envelopes before constructing the provenance-rich
  // boundary table. This keeps corpus probing cheap while preserving the same
  // exact token/case admission rule used by the typed boundary prepass.
  std::vector<std::size_t> section_envelope(catalog.entries.size());
  for (const auto &record : records)
    for (const auto &segment : record.control_segments) {
      const auto owner =
          entry_by_segment.find({record.logical_record, segment.segment_index});
      if (owner == entry_by_segment.end())
        continue;
      const auto label =
          exact_section_label(record, segment, ownership_index);
      if (!label)
        continue;
      auto &state = section_envelope[owner->second];
      const auto expected = state == 0 ? MessageSectionKind::meaning
                                      : MessageSectionKind::action;
      if (state >= 2 || label->kind != expected)
        return fail("source is not an ordered Meaning/Action message envelope");
      ++state;
    }
  if (std::any_of(section_envelope.begin(), section_envelope.end(),
                  [](const auto state) { return state != 2; }))
    return fail("source is not a complete Meaning/Action message envelope");

  for (const auto &record : records) {
    for (const auto &segment : record.control_segments) {
      const auto owner =
          entry_by_segment.find({record.logical_record, segment.segment_index});
      if (owner == entry_by_segment.end())
        continue;
      auto boundary = section_boundary_for_segment(
          record, segment, ownership_index, owner->second, rows_by_segment,
          later_continuation);
      if (boundary)
        catalog.boundaries.push_back(std::move(*boundary));
    }
  }
  std::vector<std::vector<std::size_t>> entry_boundaries(
      catalog.entries.size());
  for (std::size_t index = 0; index < catalog.boundaries.size(); ++index) {
    const auto &boundary = catalog.boundaries[index];
    if (boundary.owner_entry >= entry_boundaries.size())
      return fail("message section boundary has no owning entry");
    entry_boundaries[boundary.owner_entry].push_back(index);
  }
  for (std::size_t entry = 0; entry < entry_boundaries.size(); ++entry) {
    const auto &boundaries = entry_boundaries[entry];
    if (boundaries.size() != 2 ||
        catalog.boundaries[boundaries[0]].kind !=
            MessageSectionKind::meaning ||
        catalog.boundaries[boundaries[1]].kind !=
            MessageSectionKind::action)
      return fail("message boundary prepass lacks ordered Meaning/Action: " +
                  catalog.entries[entry].id + " count=" +
                  std::to_string(boundaries.size()) + " total=" +
                  std::to_string(catalog.boundaries.size()));
  }

  std::vector<std::optional<std::size_t>> active_section(
      catalog.entries.size());
  std::map<SegmentKey, std::size_t> normal_boundary_by_segment;
  for (std::size_t index = 0; index < catalog.boundaries.size(); ++index) {
    const auto &boundary = catalog.boundaries[index];
    if (boundary.label_source_row) {
      const auto source_run = std::find_if(
          layout.runs.begin(), layout.runs.end(), [&](const auto &run) {
            return run.id == boundary.label_source_row->first;
          });
      if (source_run == layout.runs.end() ||
          boundary.label_source_row->second >= source_run->rows.size())
        return fail("normal section boundary label row is invalid");
      const auto &source_row =
          source_run->rows[boundary.label_source_row->second];
      if (!normal_boundary_by_segment
               .emplace(SegmentKey{source_row.logical_record,
                                   source_row.segment_index},
                        index)
               .second)
        return fail("one source segment contains duplicate section labels");
    } else if (boundary.shape == MessageSectionBoundaryShapeIR::normal_row)
      return fail("normal section boundary lacks a physical label row");
  }
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
    if (run.control_kind == BookControlKind::message_start ||
        run.control_kind == BookControlKind::structural ||
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
    std::optional<std::size_t> run_boundary;
    std::optional<std::size_t> label_row_index;
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto &row = run.rows[row_index];
      const auto owner =
          entry_by_segment.find({row.logical_record, row.segment_index});
      if (owner == entry_by_segment.end())
        continue;
      const auto boundary = normal_boundary_by_segment.find(
          {row.logical_record, row.segment_index});
      if (boundary != normal_boundary_by_segment.end()) {
        const auto &typed = catalog.boundaries[boundary->second];
        if (typed.owner_entry != *run_owner)
          return fail("section boundary owner differs from display run");
        if (run_boundary && *run_boundary != boundary->second)
          return fail("one display run contains two message sections");
        run_kind = typed.kind;
        run_boundary = boundary->second;
        if (!label_row_index && typed.label_source_row &&
            *typed.label_source_row == MessageSourceRowIR{run.id, row_index}) {
          label_row_index = row_index;
        }
      }
    }
    if (run_kind) {
      if (!label_row_index)
        return fail("normal section boundary has no exact label row");
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
      section.recovered_record_continuation =
          catalog.boundaries[*run_boundary].shape !=
          MessageSectionBoundaryShapeIR::normal_row;
      section.boundary_index = *run_boundary;
      section.label_source_rows.push_back({run.id, row_index});
      section.label_source_slices.push_back(
          catalog.boundaries[*run_boundary].label_source);
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
      const auto *row_record = find_record(records, row.logical_record);
      if (row_record == nullptr ||
          row.segment_index >= row_record->control_segments.size())
        return fail("message row has no decoded source segment");
      if (row_record->control_segments[row.segment_index].kind ==
              BookControlKind::message_start ||
          message_start_overflow.count(
              {row.logical_record, row.segment_index}) != 0) {
        entry.suppressed_source_rows.push_back(source);
        continue;
      }
      const auto is_explicit_label = run_kind && label_row_index == row_index;
      const auto is_label =
          run_kind && (is_explicit_label || run_paragraph.text.empty());
      const auto final_segment_row =
          row_index + 1 == run.rows.size() ||
          run.rows[row_index + 1].logical_record != row.logical_record ||
          run.rows[row_index + 1].segment_index != row.segment_index;
      const auto initial_headline_row =
          !active_section[*run_owner] && !run_kind &&
          entry.headline.source_rows.empty() &&
          entry.headline.source_segments.empty() && run_paragraph.text.empty();
      auto preceding_text = run_paragraph.text;
      if (preceding_text.empty()) {
        if (active_section[*run_owner]) {
          const auto &paragraphs =
              entry.sections[*active_section[*run_owner]].paragraphs;
          for (const auto &paragraph : paragraphs)
            append_text(preceding_text, paragraph.text);
        } else {
          preceding_text = entry.headline.text;
          for (const auto &continuation : entry.headline_continuations)
            append_text(preceding_text, continuation.text);
        }
      }
      auto semantic = semantic_row(
          records, ownership_index, row, run, row_index, is_label,
          final_segment_row &&
              (!active_section[*run_owner] && !run_kind &&
               row_index + 1 == run.rows.size()),
          final_segment_row, initial_headline_row);
      auto terminal_context = preceding_text;
      append_text(terminal_context, semantic.text);
      if (semantic.terminal_layout_token &&
          closes_unmatched_delimiter(
              terminal_context,
              semantic.terminal_layout_token->decoded_text)) {
        semantic.text += semantic.terminal_layout_token->decoded_text;
        semantic.terminal_layout_token.reset();
      }
      if (!initial_headline_row && row.marker &&
          semantic.marker_disposition !=
              MessageMarkerDispositionIR::lexical_prefix &&
          opens_unmatched_delimiter(row.marker->decoded_text, semantic.text)) {
        semantic.marker_disposition =
            MessageMarkerDispositionIR::lexical_prefix;
        semantic.text = row.marker->decoded_text + semantic.text;
      }
      if (row.marker &&
          semantic.marker_disposition !=
              MessageMarkerDispositionIR::opaque_continuation_suffix &&
          closes_unmatched_delimiter(preceding_text,
                                     row.marker->decoded_text))
        semantic.marker_disposition =
            MessageMarkerDispositionIR::punctuation_suffix;
      auto text = semantic.text;
      if (run_kind && is_explicit_label)
        text = typed_section_payload(catalog.boundaries[*run_boundary],
                                     std::move(text));
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
      if (semantic.text.empty()) {
        if (std::find(entry.suppressed_source_rows.begin(),
                      entry.suppressed_source_rows.end(),
                      semantic.source_row) ==
            entry.suppressed_source_rows.end())
          entry.suppressed_source_rows.push_back(semantic.source_row);
        continue;
      }
      MessageParagraphIR paragraph;
      paragraph.text = semantic.text;
      paragraph.source_rows.push_back(semantic.source_row);
      paragraph.source_slices.insert(paragraph.source_slices.end(),
                                     semantic.leading_source_slices.begin(),
                                     semantic.leading_source_slices.end());
      paragraph.source_slices.insert(paragraph.source_slices.end(),
                                     semantic.trailing_source_slices.begin(),
                                     semantic.trailing_source_slices.end());
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

  for (std::size_t boundary_index = 0;
       boundary_index < catalog.boundaries.size(); ++boundary_index) {
    const auto &boundary = catalog.boundaries[boundary_index];
    if (boundary.shape == MessageSectionBoundaryShapeIR::normal_row)
      continue;
    if (boundary.owner_entry >= catalog.entries.size())
      return fail("recovered section boundary has no owning entry");
    auto &entry = catalog.entries[boundary.owner_entry];
    auto existing = std::find_if(
        entry.sections.begin(), entry.sections.end(), [&](const auto &section) {
          return section.boundary_index == boundary_index;
        });
    if (existing != entry.sections.end() &&
        std::any_of(existing->paragraphs.begin(), existing->paragraphs.end(),
                    [](const auto &paragraph) {
                      return !paragraph.text.empty();
                    }))
      continue;
    if (existing == entry.sections.end() &&
        std::any_of(entry.sections.begin(), entry.sections.end(),
                    [&](const auto &section) {
                      return section.kind == boundary.kind;
                    }))
      return fail("message entry has a duplicate typed section: " + entry.id);
    MessageSectionIR recovered_section;
    auto &section = existing == entry.sections.end() ? recovered_section
                                                     : *existing;
    section.kind = boundary.kind;
    section.logical_record = boundary.label_source.logical_record;
    section.segment_index = boundary.label_source.segment_index;
    section.recovered_record_continuation = true;
    section.boundary_index = boundary_index;
    section.label_source_rows.clear();
    section.label_source_slices.clear();
    section.label_source_slices.push_back(boundary.label_source);
    section.source_rows = boundary.payload_source_rows;
    MessageParagraphIR paragraph;
    paragraph.recovered_unformatted_segment = true;
    paragraph.source_slices = boundary.payload_source_slices;
    for (const auto &slice : boundary.payload_source_slices) {
      const auto *record = find_record(records, slice.logical_record);
      if (record == nullptr)
        return fail("typed section payload references a missing record");
      append_text(paragraph.text, source_slice_text(*record, slice));
    }
    if (paragraph.text.empty())
      return fail("typed section boundary has no source payload: " + entry.id);
    section.paragraphs.clear();
    section.paragraphs.push_back(std::move(paragraph));
    for (const auto &row : boundary.payload_source_rows)
      if (std::find(entry.suppressed_source_rows.begin(),
                    entry.suppressed_source_rows.end(), row) ==
          entry.suppressed_source_rows.end())
        entry.suppressed_source_rows.push_back(row);
    if (existing == entry.sections.end())
      entry.sections.push_back(std::move(recovered_section));
  }
  for (auto &entry : catalog.entries)
    std::stable_sort(entry.sections.begin(), entry.sections.end(),
                     [](const auto &left, const auto &right) {
                       return left.boundary_index < right.boundary_index;
                     });

  for (std::size_t entry_index = 0; entry_index < catalog.entries.size();
       ++entry_index) {
    auto &entry = catalog.entries[entry_index];
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
      if (message_start_overflow.count(coordinate) != 0)
        continue;
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
        std::vector<std::size_t> lexical_tokens;
        std::copy_if(segment.source_tokens.begin(), segment.source_tokens.end(),
                     std::back_inserter(lexical_tokens), [&](const auto token) {
                       return token < record->ir.tokens.size() &&
                              !structural_padding_token(
                                  record->ir.tokens[token]);
                     });
        // A record-prefix field that is nothing but the next display line's
        // length byte carries no text. The record's own framing says which
        // that is; only an unframed record falls back to the observed compact
        // envelope, and even then a single lexical word is preserved.
        const auto prefix_field_role =
            lexical_tokens.size() == 1
                ? source_field_role(*record, lexical_tokens.front())
                : SourceFieldRole::positioned;
        const auto terminal_marker_only =
            lexical_tokens.size() == 1 &&
            (prefix_field_role == SourceFieldRole::supplemental ||
             (prefix_field_role == SourceFieldRole::undecided &&
              record->ir.tokens[lexical_tokens.front()].encoded.width == 1 &&
              record->ir.tokens[lexical_tokens.front()].encoded.value >= 19 &&
              record->ir.tokens[lexical_tokens.front()].encoded.value <= 43));
        if (!terminal_marker_only)
          fragments.push_back({std::move(text), source, {}});
      } else {
        fragments =
            opaque_segment_fragments(*record, ownership_index, segment);
      }
      const auto indexed_rows = rows_by_segment.find(coordinate);
      static const std::vector<SegmentRow> no_rows;
      const auto &segment_rows =
          indexed_rows == rows_by_segment.end() ? no_rows : indexed_rows->second;
      for (auto &fragment : fragments) {
        const auto claims_fragment = [&](const MessageParagraphIR &paragraph) {
          return std::any_of(
              paragraph.source_slices.begin(), paragraph.source_slices.end(),
              [&](const auto &source) {
                return source.logical_record == fragment.source.logical_record &&
                       source.segment_index == fragment.source.segment_index &&
                       source.token_begin <= fragment.source.token_begin &&
                       source.token_end >= fragment.source.token_end;
              });
        };
        const auto collection_claims = [&](const auto &paragraphs) {
          return std::any_of(paragraphs.begin(), paragraphs.end(),
                             claims_fragment);
        };
        const auto boundary_claims = std::any_of(
            entry.sections.begin(), entry.sections.end(),
            [&](const auto &section) {
              const auto overlaps = [&](const auto &source) {
                return source.logical_record == fragment.source.logical_record &&
                       source.segment_index == fragment.source.segment_index &&
                       source.token_begin < fragment.source.token_end &&
                       fragment.source.token_begin < source.token_end;
              };
              return std::any_of(
                  section.label_source_slices.begin(),
                  section.label_source_slices.end(), overlaps) ||
                     (section.boundary_index &&
                      catalog.boundaries[*section.boundary_index].shape !=
                          MessageSectionBoundaryShapeIR::normal_row &&
                      std::any_of(
                          catalog.boundaries[*section.boundary_index]
                              .payload_source_slices.begin(),
                          catalog.boundaries[*section.boundary_index]
                              .payload_source_slices.end(),
                          overlaps));
            });
        if (boundary_claims || claims_fragment(entry.headline) ||
            collection_claims(entry.headline_continuations) ||
            collection_claims(entry.sections[0].paragraphs) ||
            collection_claims(entry.sections[1].paragraphs))
          continue;
        std::optional<MessageSourceRowIR> preceding_row;
        std::optional<MessageSourceRowIR> following_row;
        auto preceding_end = std::size_t{};
        auto next_begin = std::numeric_limits<std::size_t>::max();
        for (const auto &[source_row, token_begin, token_end] : segment_rows) {
          if (token_end <= fragment.source.token_begin &&
              (!preceding_row || token_end > preceding_end)) {
            preceding_row = source_row;
            preceding_end = token_end;
          }
          if (token_begin >= fragment.source.token_end &&
              token_begin < next_begin) {
            following_row = source_row;
            next_begin = token_begin;
          }
        }
        const auto find_physical_row = [&](const MessageSourceRowIR &source)
            -> const PhysicalRowIR * {
          const auto run = std::find_if(
              layout.runs.begin(), layout.runs.end(),
              [&](const auto &candidate) { return candidate.id == source.first; });
          if (run == layout.runs.end() || source.second >= run->rows.size())
            return nullptr;
          return &run->rows[source.second];
        };
        const auto rebuild_paragraph_text = [](MessageParagraphIR &paragraph) {
          paragraph.text.clear();
          for (const auto &row : paragraph.semantic_rows)
            append_text(paragraph.text, row.text);
        };
        const auto attach_to_following_row = [&](MessageParagraphIR &paragraph) {
          if (!following_row || fragment.source.token_end > next_begin ||
              std::find(paragraph.source_rows.begin(),
                        paragraph.source_rows.end(), *following_row) ==
                  paragraph.source_rows.end())
            return false;
          const auto *physical = find_physical_row(*following_row);
          if (physical == nullptr || !physical->marker ||
              physical->marker->token_index < fragment.source.token_end ||
              physical->marker->token_index > next_begin ||
              !closes_unmatched_delimiter(fragment.text,
                                          physical->marker->decoded_text))
            return false;
          for (auto token = fragment.source.token_end;
               token < physical->marker->token_index; ++token)
            if (token >= record->ir.tokens.size() ||
                !structural_padding_token(record->ir.tokens[token]))
              return false;
          const auto semantic = std::find_if(
              paragraph.semantic_rows.begin(), paragraph.semantic_rows.end(),
              [&](const auto &row) { return row.source_row == *following_row; });
          if (semantic == paragraph.semantic_rows.end())
            return false;
          semantic->text = fragment.text + physical->marker->decoded_text +
                           (semantic->text.empty() ? std::string{}
                                                   : std::string{" "}) +
                           semantic->text;
          semantic->marker_disposition =
              MessageMarkerDispositionIR::closing_delimiter_bridge;
          semantic->leading_source_slices.push_back(fragment.source);
          paragraph.source_slices.push_back(fragment.source);
          paragraph.suppressed_layout_tokens.insert(
              paragraph.suppressed_layout_tokens.end(),
              fragment.suppressed_layout_tokens.begin(),
              fragment.suppressed_layout_tokens.end());
          rebuild_paragraph_text(paragraph);
          return true;
        };
        const auto attach_to_paragraph = [&](MessageParagraphIR &paragraph) {
          if (!preceding_row || fragment.source.token_end > next_begin)
            return false;
          if (std::find(paragraph.source_rows.begin(),
                        paragraph.source_rows.end(), *preceding_row) ==
              paragraph.source_rows.end())
            return false;
          append_text(paragraph.text, fragment.text);
          paragraph.source_slices.push_back(fragment.source);
          paragraph.suppressed_layout_tokens.insert(
              paragraph.suppressed_layout_tokens.end(),
              fragment.suppressed_layout_tokens.begin(),
              fragment.suppressed_layout_tokens.end());
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
        const auto attach_to_next_row = [&](auto &paragraphs) {
          return std::any_of(paragraphs.begin(), paragraphs.end(),
                             attach_to_following_row);
        };
        if (coordinate < meaning_coordinate) {
          if (attach_to_next_row(entry.headline_continuations) ||
              attach_to_following_row(entry.headline) ||
              attach_to_preceding_row(entry.headline_continuations) ||
              attach_to_paragraph(entry.headline))
            continue;
        } else if (coordinate < action_coordinate) {
          if (attach_to_next_row(entry.sections[0].paragraphs) ||
              attach_to_preceding_row(entry.sections[0].paragraphs))
            continue;
        } else if (attach_to_next_row(entry.sections[1].paragraphs) ||
                   attach_to_preceding_row(entry.sections[1].paragraphs)) {
          continue;
        }
        MessageParagraphIR recovered;
        recovered.text = std::move(fragment.text);
        recovered.recovered_unformatted_segment = true;
        if (recovered.text.empty())
          continue;
        recovered.source_slices.push_back(fragment.source);
        recovered.suppressed_layout_tokens =
            std::move(fragment.suppressed_layout_tokens);
        if (coordinate < meaning_coordinate) {
          entry.headline_continuations.push_back(std::move(recovered));
        } else if (coordinate < action_coordinate) {
          if (!recovered.text.empty())
            entry.sections[0].paragraphs.push_back(std::move(recovered));
        } else {
          if (!recovered.text.empty())
            entry.sections[1].paragraphs.push_back(std::move(recovered));
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
    for (auto &section : entry.sections) {
      for (const auto &paragraph : section.paragraphs)
        if (paragraph.text.empty())
          for (const auto &row : paragraph.source_rows)
            if (std::find(entry.suppressed_source_rows.begin(),
                          entry.suppressed_source_rows.end(), row) ==
                entry.suppressed_source_rows.end())
              entry.suppressed_source_rows.push_back(row);
      section.paragraphs.erase(std::remove_if(section.paragraphs.begin(),
                                              section.paragraphs.end(),
                                              [](const auto &paragraph) {
                                                return paragraph.text.empty();
                                              }),
                               section.paragraphs.end());
    }

    // Opaque fragments are attached and source-sorted after physical rows.
    // Re-evaluate a row's tentative terminal suppression in that final source
    // order so a delimiter can close a field introduced by an intervening
    // opaque fragment.
    auto headline_context = std::string{};
    const auto restore_terminal_delimiter = [&](MessageParagraphIR &paragraph) {
      for (auto &row : paragraph.semantic_rows) {
        auto context = headline_context;
        append_text(context, row.text);
        if (row.terminal_layout_token &&
            closes_unmatched_delimiter(
                context, row.terminal_layout_token->decoded_text)) {
          row.text += row.terminal_layout_token->decoded_text;
          paragraph.text += row.terminal_layout_token->decoded_text;
          row.terminal_layout_token.reset();
        }
      }
      append_text(headline_context, paragraph.text);
    };
    if (!entry.headline.text.empty())
      restore_terminal_delimiter(entry.headline);
    for (auto &continuation : entry.headline_continuations)
      restore_terminal_delimiter(continuation);

    if (entry.headline.text.empty() &&
        !entry.headline_continuations.empty() &&
        starts_once_with_message_id(entry.headline_continuations.front().text,
                                    entry.id)) {
      entry.headline = std::move(entry.headline_continuations.front());
      entry.headline_continuations.erase(
          entry.headline_continuations.begin());
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
          const auto payload = collapse_ascii_whitespace(
              trim_ascii(range_text(*source, font->payload_range)));
          if (!payload.empty()) {
            entry.headline.text = entry.id;
            append_text(entry.headline.text, payload);
            entry.headline.source_segments.push_back(
                {source->logical_record, font->segment_index});
          }
        }
      }
    }
    if (entry.headline.text.empty() ||
        (entry.source_rows.empty() && entry.headline.source_segments.empty()))
      return fail("message entry lacks a source-proven headline: " + entry.id);
    auto complete_headline = entry.headline.text;
    for (const auto &continuation : entry.headline_continuations)
      append_text(complete_headline, continuation.text);
    if (!starts_once_with_message_id(complete_headline, entry.id))
      return fail("message headline does not begin exactly once with its ID: " +
                  entry.id + " [" + complete_headline + "]");
    if (!balanced_angle_placeholders(complete_headline))
      return fail("message headline has unbalanced placeholders: " + entry.id +
                  " [" + complete_headline + "]");
    for (auto &section : entry.sections) {
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
  const auto boundary_equal = [](const auto &a, const auto &b) {
    return a.kind == b.kind && a.owner_entry == b.owner_entry &&
           a.shape == b.shape && a.label_source == b.label_source &&
           a.label_source_row == b.label_source_row &&
           a.payload_source_slices == b.payload_source_slices &&
           a.payload_source_rows == b.payload_source_rows;
  };
  return left.boundaries.size() == right.boundaries.size() &&
         std::equal(left.boundaries.begin(), left.boundaries.end(),
                    right.boundaries.begin(), boundary_equal) &&
         left.entries.size() == right.entries.size() &&
         std::equal(left.entries.begin(), left.entries.end(),
                    right.entries.begin(), entry_equal);
}

std::string format_message_catalog_ir(const MessageCatalogIR &catalog) {
  std::ostringstream out;
  out << "message_catalog entries=" << catalog.entries.size()
      << " boundaries=" << catalog.boundaries.size() << '\n';
  for (std::size_t index = 0; index < catalog.boundaries.size(); ++index) {
    const auto &boundary = catalog.boundaries[index];
    out << "boundary index=" << index << " owner=" << boundary.owner_entry
        << " kind=" << section_name(boundary.kind)
        << " shape=" << boundary_shape_name(boundary.shape) << " label="
        << boundary.label_source.logical_record << ':'
        << boundary.label_source.segment_index << " tokens=["
        << boundary.label_source.token_begin << ','
        << boundary.label_source.token_end << ") bytes=["
        << boundary.label_source.byte_begin << ','
        << boundary.label_source.byte_end << ") payload_slices="
        << boundary.payload_source_slices.size() << '\n';
  }
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
