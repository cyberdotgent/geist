#include "geist/detail/message_topic_ir.hpp"

#include "geist/detail/internal.hpp"
#include "geist/detail/selector_ir.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace geist::detail {
namespace {

using SegmentKey = std::pair<std::uint32_t, std::size_t>;

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

std::optional<MessageTopicIR>
extract_message_topic_ir(const std::vector<DecodedLogicalRecordSource> &records,
                         const LayoutIR &layout, const OwnershipIR &ownership,
                         std::string *error) {
  const auto reject =
      [&](std::string message) -> std::optional<MessageTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty())
    return reject("message topic has no logical records");
  std::string verification_error;
  if (!verify_layout_ir(records, layout, &verification_error) ||
      !verify_ownership_ir(records, layout, ownership, &verification_error))
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

  auto catalog = extract_message_catalog_ir(records, layout, ownership,
                                            &verification_error);
  if (!catalog)
    return reject("inner message catalog rejected: " + verification_error);
  if (catalog->entries.size() != 396 || catalog->entries.front().id != "023" ||
      catalog->entries.back().id != "2505")
    return reject("message catalog fixture boundary is not canonical");
  result.catalog = std::move(*catalog);

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
  if (result.segments.size() != ordered.size() ||
      result.anchors.size() != result.catalog.entries.size() + 2)
    return reject("message source ledger or anchor set is incomplete");
  result.terminal_content_source = result.segments.back().source;
  if (result.segments.back().role != MessageTopicSegmentRoleIR::catalog ||
      result.segments.back().kind != BookControlKind::font)
    return reject("message topic terminal Action content is not conserved");

  if (error != nullptr)
    error->clear();
  return result;
}

bool verify_message_topic_ir(
    const std::vector<DecodedLogicalRecordSource> &records,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const MessageTopicIR &topic, std::string *error) {
  if (!verify_message_catalog_ir(records, layout, ownership, topic.catalog,
                                 error))
    return false;
  const auto canonical =
      extract_message_topic_ir(records, layout, ownership, error);
  if (!canonical)
    return false;
  if (!same_topic_envelope(*canonical, topic))
    return fail(error, "message topic differs from canonical extraction");
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_message_topic_ir(const MessageTopicIR &topic) {
  std::ostringstream out;
  out << "message_topic records=[" << topic.first_logical_record << ','
      << topic.end_logical_record << ") id=" << topic.metadata.raw_topic_id
      << " heading=" << topic.metadata.heading_level << " title='"
      << topic.title
      << "' introduction_rows=" << topic.introduction_row_indices.size()
      << " anchors=" << topic.anchors.size()
      << " selectors=" << topic.selectors.size()
      << " rows=" << topic.rows.size() << " segments=" << topic.segments.size()
      << " terminal=" << topic.terminal_content_source.logical_record << ':'
      << topic.terminal_content_source.segment_index << '\n'
      << format_message_catalog_ir(topic.catalog);
  return out.str();
}

} // namespace geist::detail
