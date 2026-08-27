#include "geist/detail/message_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
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
                             a.source_rows == b.source_rows &&
                             a.source_segments == b.source_segments &&
                             a.semantic_rows == b.semantic_rows;
                    });
}

bool entry_equal(const MessageEntryIR &left, const MessageEntryIR &right) {
  if (left.id != right.id || left.logical_record != right.logical_record ||
      left.segment_index != right.segment_index ||
      left.headline.text != right.headline.text ||
      left.headline.source_rows != right.headline.source_rows ||
      left.headline.source_segments != right.headline.source_segments ||
      left.headline.semantic_rows != right.headline.semantic_rows ||
      left.headline_continuations.size() !=
          right.headline_continuations.size() ||
      left.sections.size() != right.sections.size())
    return false;
  for (std::size_t index = 0; index < left.headline_continuations.size();
       ++index)
    if (left.headline_continuations[index].text !=
            right.headline_continuations[index].text ||
        left.headline_continuations[index].source_rows !=
            right.headline_continuations[index].source_rows ||
        left.headline_continuations[index].source_segments !=
            right.headline_continuations[index].source_segments ||
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
      // In this message catalog, compact values 28..43 form the observed
      // terminal row-control alphabet. Their token-map spellings (a, action,
      // agent, be, bridge, by, ...) are layout evidence when they occupy this
      // exact pre-section run boundary, not visible message prose. Value 44
      // (`can`) and higher remain lexical.
      if (source.encoded.width == 1 && source.encoded.value <= 43 &&
          alphabetic) {
        suppressed_token = token;
        if (terminal_layout_token != nullptr) {
          MessageTerminalLayoutTokenIR evidence;
          evidence.logical_record = row.logical_record;
          evidence.token_index = token;
          evidence.encoded = source.encoded;
          evidence.bytes = source.byte_range;
          const auto decoded = token_words_to_ascii(source.decoded_words);
          evidence.decoded_text = trim_ascii(decoded);
          *terminal_layout_token = std::move(evidence);
        }
      }
    }
  }
  const auto token_end =
      std::min(row.token_end, indexed_outputs->second.size());
  for (auto token = row.token_begin; token < token_end; ++token) {
    if (suppressed_token == token)
      continue;
    for (const auto output : indexed_outputs->second[token]) {
      const auto &source = record->assembled.sources[output];
      if (source.kind == LogicalWordSourceKind::inserted_space) {
        if (visible_tokens.count(source.token_index) != 0)
          result.push_back(' ');
        continue;
      }
      const auto found =
          visible_cells.find({source.token_index, source.word_index});
      if (found == visible_cells.end() || found->second->word > 0xff ||
          unmapped_cell(*record, *found->second))
        continue;
      result.push_back(static_cast<char>(found->second->word));
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

std::string opaque_text_before_segment(
    const std::vector<DecodedLogicalRecordSource> &records,
    const MessageOwnershipIndex &ownership, std::uint32_t logical_record,
    std::size_t segment_index) {
  const auto *record = find_record(records, logical_record);
  if (record == nullptr || segment_index == 0 ||
      segment_index >= record->control_segments.size())
    return {};
  const auto end =
      decoded_byte_range_to_word_range(
          record->assembled,
          {0, record->control_segments[segment_index].complete.begin})
          .end;
  std::string result;
  for (std::size_t output = 0;
       output < end && output < record->assembled.sources.size(); ++output) {
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
    if (found == ownership.cells.end() ||
        found->second->disposition != SourceDisposition::opaque ||
        found->second->word > 0xff || unmapped_cell(*record, *found->second))
      continue;
    result.push_back(static_cast<char>(found->second->word));
  }
  return collapse_ascii_whitespace(trim_ascii(std::move(result)));
}

MessageMarkerDispositionIR marker_disposition(const PhysicalRowIR &row,
                                              bool section_label,
                                              const std::string &preceding,
                                              std::size_t row_index) {
  if (!row.marker)
    return MessageMarkerDispositionIR::absent;
  if (section_label)
    return MessageMarkerDispositionIR::layout_artifact;
  const auto &marker = *row.marker;
  const auto single = marker.decoded_text.size() == 1
                          ? static_cast<unsigned char>(marker.decoded_text[0])
                          : 0;
  if (single == '.' || single == ',' || single == ':' || single == ';' ||
      single == '!' || single == '?') {
    // Value 4 is the catalog's mechanical soft-wrap placeholder, even though
    // its token-map projection is a question mark.
    if (marker.encoded_value == 4)
      return MessageMarkerDispositionIR::layout_artifact;
    return MessageMarkerDispositionIR::punctuation_suffix;
  }
  if ((single == ')' &&
       std::count(preceding.begin(), preceding.end(), '(') >
           std::count(preceding.begin(), preceding.end(), ')')) ||
      (single == ']' &&
       std::count(preceding.begin(), preceding.end(), '[') >
           std::count(preceding.begin(), preceding.end(), ']')) ||
      (single == '}' &&
       std::count(preceding.begin(), preceding.end(), '{') >
           std::count(preceding.begin(), preceding.end(), '}')))
    return MessageMarkerDispositionIR::punctuation_suffix;
  if (single == '-' && row_index == 0 && row.native_origin != 3)
    return MessageMarkerDispositionIR::list_prefix;
  const auto lexical =
      !marker.decoded_text.empty() &&
      std::all_of(marker.decoded_text.begin(), marker.decoded_text.end(),
                  [](const unsigned char ch) {
                    return std::isalnum(ch) != 0 || ch == '_';
                  });
  if (lexical && (row.native_origin != 3 || marker.encoded_value >= 40))
    return MessageMarkerDispositionIR::lexical_prefix;
  return MessageMarkerDispositionIR::layout_artifact;
}

MessageSemanticRowIR
semantic_row(const std::vector<DecodedLogicalRecordSource> &records,
             const MessageOwnershipIndex &ownership, const PhysicalRowIR &row,
             DisplayRunId run, std::size_t row_index, bool section_label,
             const std::string &preceding,
             bool suppress_terminal_layout_word = false) {
  MessageSemanticRowIR result;
  result.source_row = {run, row_index};
  auto visible = collapse_ascii_whitespace(trim_ascii(
      owned_row_text(ownership, row, row_index, suppress_terminal_layout_word,
                     &result.terminal_layout_token)));
  const auto prefix = opaque_continuation_prefix(records, ownership, row);
  if (!prefix.empty())
    visible =
        prefix + (visible.empty() ? std::string{} : std::string{" "}) + visible;
  result.marker_disposition =
      marker_disposition(row, section_label, preceding, row_index);
  if (row.marker &&
      result.marker_disposition == MessageMarkerDispositionIR::lexical_prefix)
    visible = row.marker->decoded_text +
              (visible.empty() ? std::string{} : std::string{" "}) + visible;
  else if (row.marker &&
           result.marker_disposition == MessageMarkerDispositionIR::list_prefix)
    visible = row.marker->decoded_text +
              (visible.empty() ? std::string{} : std::string{" "}) + visible;
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
      auto semantic = semantic_row(records, ownership, row, run.id, row_index,
                                   false, paragraph.text);
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
      auto semantic = semantic_row(records, ownership, row, run.id, row_index,
                                   false, paragraph.text);
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
      std::remove_if(
          entry.headline_continuations.begin(),
          entry.headline_continuations.end(),
          [&](const auto &paragraph) { return paragraph.source_rows == rows; }),
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
      auto semantic = semantic_row(records, ownership_index, row, run.id,
                                   row_index, is_label, run_paragraph.text,
                                   !active_section[*run_owner] && !run_kind &&
                                       row_index + 1 == run.rows.size());
      auto text = semantic.text;
      if (run_kind && is_explicit_label && explicit_label)
        text = section_payload(*run_kind, std::move(text));
      if (row.marker &&
          semantic.marker_disposition ==
              MessageMarkerDispositionIR::punctuation_suffix &&
          (run_paragraph.text.empty() ||
           run_paragraph.text.back() != row.marker->decoded_text.front()))
        run_paragraph.text += row.marker->decoded_text;
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
    if (active_section[*run_owner]) {
      auto &section = entry.sections[*active_section[*run_owner]];
      auto &paragraph = section.paragraphs.back();
      append_text(paragraph.text, run_paragraph.text);
      paragraph.source_rows.insert(paragraph.source_rows.end(),
                                   run_paragraph.source_rows.begin(),
                                   run_paragraph.source_rows.end());
      paragraph.source_segments.insert(paragraph.source_segments.end(),
                                       run_paragraph.source_segments.begin(),
                                       run_paragraph.source_segments.end());
      paragraph.semantic_rows.insert(paragraph.semantic_rows.end(),
                                     run_paragraph.semantic_rows.begin(),
                                     run_paragraph.semantic_rows.end());
      section.source_rows.insert(section.source_rows.end(),
                                 run_paragraph.source_rows.begin(),
                                 run_paragraph.source_rows.end());
    } else if (entry.headline.source_rows.empty()) {
      entry.headline = std::move(run_paragraph);
    } else {
      entry.headline_continuations.push_back(std::move(run_paragraph));
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
           section.paragraphs.front().source_segments.empty())) {
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
