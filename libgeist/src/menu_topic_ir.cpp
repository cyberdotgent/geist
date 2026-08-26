#include "geist/detail/menu_topic_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

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
                                   const ControlSegmentIR &segment) {
  DocumentSourceSliceIR result;
  result.logical_record = record.logical_record;
  result.segment_index = segment.segment_index;
  if (segment.source_tokens.empty())
    return result;
  result.token_begin = segment.source_tokens.front();
  result.token_end = segment.source_tokens.back() + 1;
  result.byte_begin = record.ir.tokens[result.token_begin].byte_range.begin;
  result.byte_end = record.ir.tokens[result.token_end - 1].byte_range.end;
  return result;
}

std::vector<MenuSourceCellIR>
source_cells(const DecodedLogicalRecordSource &record,
             const OutputRangeIR &range) {
  std::vector<MenuSourceCellIR> result;
  const auto words = decoded_byte_range_to_word_range(record.assembled, range);
  for (auto output = words.begin; output < words.end; ++output) {
    if (output >= record.assembled.words.size() ||
        output >= record.assembled.sources.size())
      return {};
    const auto &source = record.assembled.sources[output];
    if (source.token_index >= record.ir.tokens.size())
      return {};
    result.push_back({record.logical_record, output, source.token_index,
                      source.word_index,
                      source.kind == LogicalWordSourceKind::token_word
                          ? MenuSourceCellKind::token_word
                          : MenuSourceCellKind::inserted_space,
                      record.assembled.words[output],
                      record.ir.tokens[source.token_index].byte_range});
  }
  return result;
}

bool valid_anchor(const std::string &value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
         });
}

using CellKey = std::tuple<std::uint32_t, std::size_t>;

bool insert_unique_cells(std::set<CellKey> &owned,
                         const std::vector<MenuSourceCellIR> &cells) {
  for (const auto &cell : cells)
    if (!owned.emplace(cell.logical_record, cell.output_word_index).second)
      return false;
  return true;
}

bool same_slice(const DocumentSourceSliceIR &left,
                const DocumentSourceSliceIR &right) {
  return left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.token_begin == right.token_begin &&
         left.token_end == right.token_end &&
         left.byte_begin == right.byte_begin && left.byte_end == right.byte_end;
}

bool same_cell(const MenuSourceCellIR &left, const MenuSourceCellIR &right) {
  return left.logical_record == right.logical_record &&
         left.output_word_index == right.output_word_index &&
         left.token_index == right.token_index &&
         left.word_index == right.word_index && left.kind == right.kind &&
         left.word == right.word &&
         left.token_bytes.begin == right.token_bytes.begin &&
         left.token_bytes.end == right.token_bytes.end;
}

bool same_cells(const std::vector<MenuSourceCellIR> &left,
                const std::vector<MenuSourceCellIR> &right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(), same_cell);
}

bool same_topic(const MenuTopicIR &left, const MenuTopicIR &right) {
  if (left.heading_level != right.heading_level || left.title != right.title ||
      !same_slice(left.title_source, right.title_source) ||
      !same_cells(left.title_cells, right.title_cells) ||
      left.anchor.has_value() != right.anchor.has_value() ||
      left.items.size() != right.items.size() ||
      left.segments.size() != right.segments.size())
    return false;
  if (left.anchor && (left.anchor->id != right.anchor->id ||
                      !same_slice(left.anchor->source, right.anchor->source)))
    return false;
  for (std::size_t index = 0; index < left.items.size(); ++index) {
    const auto &a = left.items[index];
    const auto &b = right.items[index];
    if (a.target.kind != b.target.kind || a.target.value != b.target.value ||
        a.label != b.label || !same_slice(a.source, b.source) ||
        !same_cells(a.target_cells, b.target_cells) ||
        !same_cells(a.label_cells, b.label_cells))
      return false;
  }
  for (std::size_t index = 0; index < left.segments.size(); ++index) {
    const auto &a = left.segments[index];
    const auto &b = right.segments[index];
    if (a.kind != b.kind || a.opcode != b.opcode ||
        !same_slice(a.source, b.source))
      return false;
  }
  return true;
}

} // namespace

std::optional<MenuTopicIR>
extract_menu_topic_ir(const std::vector<DecodedLogicalRecordSource> &records,
                      const MenuIR &menu, const LayoutIR &layout,
                      const OwnershipIR &ownership, std::string *error) {
  const auto reject = [&](std::string message) -> std::optional<MenuTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty() || menu.items.empty())
    return reject("menu topic source or semantic menu is empty");
  std::string inner_error;
  if (!verify_layout_ir(records, layout, &inner_error) ||
      !verify_ownership_ir(records, layout, ownership, &inner_error))
    return reject("menu topic prerequisite IR rejected: " + inner_error);

  struct SegmentRef {
    const DecodedLogicalRecordSource *record;
    const ControlSegmentIR *segment;
  };
  std::vector<SegmentRef> segments;
  for (const auto &record : records)
    for (const auto &segment : record.control_segments) {
      if (segment.source_tokens.empty())
        return reject("menu topic segment lacks exact source provenance");
      segments.push_back({&record, &segment});
    }

  const std::vector<BookControlKind> metadata = {
      BookControlKind::topic_start,   BookControlKind::topic_number,
      BookControlKind::parent,        BookControlKind::forward_level,
      BookControlKind::back_level,    BookControlKind::summary,
      BookControlKind::heading_level, BookControlKind::source_file};
  if (segments.size() < metadata.size() + 4)
    return reject("menu topic envelope is incomplete");
  for (std::size_t index = 0; index < metadata.size(); ++index) {
    const auto &segment = *segments[index].segment;
    if (segment.kind != metadata[index])
      return reject("menu topic metadata is incomplete or out of order");
    if (segment.payload_range.begin != segment.payload_range.end)
      return reject("menu topic metadata contains trailing visible content");
    if (segment.malformed && segment.kind != BookControlKind::forward_level &&
        segment.kind != BookControlKind::back_level)
      return reject("menu topic metadata is malformed");
  }

  MenuTopicIR result;
  auto heading = ascii_lower(trim_ascii(
      range_text(*segments[6].record, segments[6].segment->operand_range)));
  if (!heading.empty() && heading.front() == ':')
    heading.erase(heading.begin());
  if (heading.size() != 2 || heading.front() != 'h' || heading.back() < '1' ||
      heading.back() > '6')
    return reject("menu topic heading level is invalid");
  result.heading_level = std::move(heading);

  auto index = metadata.size();
  if (segments[index].segment->kind == BookControlKind::structural) {
    const auto &segment = *segments[index].segment;
    const auto opcode = ascii_lower(segment.opcode);
    if (segment.malformed || opcode.rfind("sr", 0) != 0 ||
        segment.payload_range.begin != segment.payload_range.end)
      return reject("menu topic pre-title control is not a source anchor");
    const auto id = segment.opcode.substr(2);
    if (!valid_anchor(id))
      return reject("menu topic source anchor is invalid");
    result.anchor =
        MenuTopicAnchorIR{id, source_slice(*segments[index].record, segment)};
    ++index;
  }
  if (index >= segments.size() ||
      segments[index].segment->kind != BookControlKind::title ||
      segments[index].segment->malformed)
    return reject("menu topic has no canonical ST title");
  const auto &title_segment = *segments[index].segment;
  result.title = collapse_ascii_whitespace(trim_ascii(
      range_text(*segments[index].record, title_segment.payload_range)));
  result.title_source = source_slice(*segments[index].record, title_segment);
  result.title_cells =
      source_cells(*segments[index].record, title_segment.payload_range);
  if (result.title.empty() || result.title_cells.empty())
    return reject("menu topic title text or provenance is empty");
  ++index;

  if (index >= segments.size() ||
      segments[index].segment->kind != BookControlKind::menu_start ||
      segments[index].segment->malformed ||
      segments[index].segment->payload_range.begin !=
          segments[index].segment->payload_range.end)
    return reject("menu topic CMENU boundary is missing or has content");
  ++index;

  std::set<CellKey> visible_cells;
  if (!insert_unique_cells(visible_cells, result.title_cells))
    return reject("menu topic title source cells overlap");
  std::size_t menu_index = 0;
  while (index < segments.size() &&
         segments[index].segment->kind == BookControlKind::menu_item) {
    if (menu_index >= menu.items.size())
      return reject("menu topic contains an unverified extra CMITEM");
    const auto &segment = *segments[index].segment;
    const auto &item = menu.items[menu_index];
    if (segment.malformed ||
        item.logical_record != segments[index].record->logical_record ||
        item.segment_index != segment.segment_index)
      return reject("menu topic CMITEM does not match verified menu order");
    if (!insert_unique_cells(visible_cells, item.target_cells) ||
        !insert_unique_cells(visible_cells, item.label_cells))
      return reject("menu topic visible source cell is owned more than once");
    MenuTopicItemIR semantic;
    semantic.target = {CrossReferenceTargetKindIR::topic, item.target};
    semantic.label = item.text;
    semantic.source = source_slice(*segments[index].record, segment);
    semantic.target_cells = item.target_cells;
    semantic.label_cells = item.label_cells;
    result.items.push_back(std::move(semantic));
    ++menu_index;
    ++index;
  }
  if (menu_index != menu.items.size())
    return reject("menu topic did not consume every verified menu item");
  if (index >= segments.size() ||
      segments[index].segment->kind != BookControlKind::menu_end ||
      segments[index].segment->malformed ||
      segments[index].segment->payload_range.begin !=
          segments[index].segment->payload_range.end)
    return reject("menu topic CEMENU boundary is missing or has content");
  ++index;
  if (index != segments.size())
    return reject("menu topic contains controls or content after CEMENU");

  for (const auto &entry : segments)
    result.segments.push_back({entry.segment->kind, entry.segment->opcode,
                               source_slice(*entry.record, *entry.segment)});
  if (error != nullptr)
    error->clear();
  return result;
}

bool verify_menu_topic_ir(
    const std::vector<DecodedLogicalRecordSource> &records, const MenuIR &menu,
    const LayoutIR &layout, const OwnershipIR &ownership,
    const MenuTopicIR &topic, std::string *error) {
  const auto canonical =
      extract_menu_topic_ir(records, menu, layout, ownership, error);
  if (!canonical)
    return false;
  if (!same_topic(*canonical, topic))
    return fail(error, "menu topic differs from canonical extraction");
  if (error != nullptr)
    error->clear();
  return true;
}

std::string format_menu_topic_ir(const MenuTopicIR &topic) {
  std::ostringstream out;
  out << "menu_topic heading_level=" << topic.heading_level << " title='"
      << topic.title << "' items=" << topic.items.size()
      << " segments=" << topic.segments.size();
  if (topic.anchor)
    out << " anchor='" << topic.anchor->id << "'";
  out << '\n';
  for (const auto &item : topic.items)
    out << "item target_kind=" << static_cast<unsigned>(item.target.kind)
        << " target='" << item.target.value << "' label='" << item.label
        << "' source=" << item.source.logical_record << ':'
        << item.source.segment_index
        << " target_cells=" << item.target_cells.size()
        << " label_cells=" << item.label_cells.size() << '\n';
  for (const auto &segment : topic.segments)
    out << "segment=" << segment.source.logical_record << ':'
        << segment.source.segment_index << " opcode='" << segment.opcode
        << "'\n";
  return out.str();
}

} // namespace geist::detail
