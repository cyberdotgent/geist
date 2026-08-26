#include "geist/detail/menu_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace geist::detail {
namespace {

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

std::string normalized(std::string value) {
  return collapse_ascii_whitespace(trim_ascii(std::move(value)));
}

std::string visible_token(const DecodedLogicalRecordSource& record,
                          std::size_t token) {
  if (token >= record.tokens.size()) return {};
  auto words = record.tokens[token];
  if (!words.empty() && words.front() < 4) words.erase(words.begin());
  words.erase(std::remove(words.begin(), words.end(), 0x2666), words.end());
  return normalized(token_words_to_ascii(words));
}

const std::string* find_title(
    const std::map<std::string, std::string>& titles,
    const std::string& target) {
  const auto exact = titles.find(target);
  if (exact != titles.end()) return &exact->second;
  const auto found = std::find_if(titles.begin(), titles.end(),
                                  [&](const auto& entry) {
    return ascii_equals_case_insensitive(entry.first, target);
  });
  return found == titles.end() ? nullptr : &found->second;
}

std::vector<MenuSourceCellIR> source_cells(
    const DecodedLogicalRecordSource& record, const OutputRangeIR& range) {
  std::vector<MenuSourceCellIR> result;
  const auto words = decoded_byte_range_to_word_range(record.assembled, range);
  for (auto output = words.begin; output < words.end; ++output) {
    if (output >= record.assembled.words.size() ||
        output >= record.assembled.sources.size())
      return {};
    const auto& source = record.assembled.sources[output];
    if (source.token_index >= record.ir.tokens.size()) return {};
    const auto& token = record.ir.tokens[source.token_index];
    result.push_back(
        {record.logical_record,
         output,
         source.token_index,
         source.word_index,
         source.kind == LogicalWordSourceKind::token_word
             ? MenuSourceCellKind::token_word
             : MenuSourceCellKind::inserted_space,
         record.assembled.words[output],
         token.byte_range});
  }
  return result;
}

bool same_cells(const std::vector<MenuSourceCellIR>& left,
                const std::vector<MenuSourceCellIR>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    const auto& a = left[index];
    const auto& b = right[index];
    if (a.logical_record != b.logical_record ||
        a.output_word_index != b.output_word_index ||
        a.token_index != b.token_index || a.word_index != b.word_index ||
        a.kind != b.kind || a.word != b.word ||
        a.token_bytes.begin != b.token_bytes.begin ||
        a.token_bytes.end != b.token_bytes.end)
      return false;
  }
  return true;
}

} // namespace

std::optional<MenuIR> extract_source_menu_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::string* error) {
  const auto fail = [&](const std::string& message) -> std::optional<MenuIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  MenuIR menu;
  bool in_menu = false;
  bool saw_menu = false;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (segment.kind == BookControlKind::menu_start) {
        if (in_menu || saw_menu) return fail("nested or repeated menu");
        in_menu = true;
        saw_menu = true;
        continue;
      }
      if (segment.kind == BookControlKind::menu_end) {
        if (!in_menu) return fail("menu end without menu start");
        in_menu = false;
        continue;
      }
      if (segment.kind != BookControlKind::menu_item) continue;
      if (!in_menu || segment.malformed || segment.source_tokens.empty())
        return fail("menu item is outside a valid menu");

      MenuItemIR item;
      item.logical_record = record.logical_record;
      item.segment_index = segment.segment_index;
      item.target = normalized(range_text(record, segment.operand_range));
      item.text = normalized(range_text(record, segment.payload_range));
      if (item.target.empty() || item.text.empty())
        return fail("menu item target or label is unavailable: " +
                    item.target);
      item.target_output = segment.operand_range;
      item.label_output = segment.payload_range;
      item.target_cells = source_cells(record, segment.operand_range);
      item.label_cells = source_cells(record, segment.payload_range);
      if (item.target_cells.empty() || item.label_cells.empty())
        return fail("menu item target or label has no exact source cells: " +
                    item.target);
      menu.items.push_back(std::move(item));
    }
  }
  if (!saw_menu || in_menu || menu.items.empty())
    return fail("source does not contain one complete non-empty menu");
  if (error != nullptr) error->clear();
  return menu;
}

bool verify_source_menu_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const MenuIR& menu, std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const auto canonical = extract_source_menu_ir(records);
  if (!canonical) return fail("source does not admit a canonical raw menu");
  if (canonical->items.size() != menu.items.size())
    return fail("menu item count differs from canonical source lowering");
  for (std::size_t index = 0; index < menu.items.size(); ++index) {
    const auto& actual = menu.items[index];
    const auto& expected = canonical->items[index];
    if (actual.logical_record != expected.logical_record ||
        actual.segment_index != expected.segment_index ||
        actual.target != expected.target || actual.text != expected.text ||
        actual.target_output.begin != expected.target_output.begin ||
        actual.target_output.end != expected.target_output.end ||
        actual.label_output.begin != expected.label_output.begin ||
        actual.label_output.end != expected.label_output.end ||
        !same_cells(actual.target_cells, expected.target_cells) ||
        !same_cells(actual.label_cells, expected.label_cells) ||
        actual.terminal_marker_token.has_value() ||
        actual.terminal_marker_encoded.has_value() ||
        actual.terminal_marker_bytes.has_value() ||
        actual.terminal_marker_display_cells.has_value())
      return fail("raw menu item text or provenance differs from source");
  }
  if (error != nullptr) error->clear();
  return true;
}

std::optional<MenuIR> extract_menu_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::map<std::string, std::string>& topic_titles,
    std::string* error) {
  const auto fail = [&](const std::string& message) -> std::optional<MenuIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  MenuIR menu;
  bool in_menu = false;
  bool saw_menu = false;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (segment.kind == BookControlKind::menu_start) {
        if (in_menu || saw_menu) return fail("nested or repeated menu");
        in_menu = true;
        saw_menu = true;
        continue;
      }
      if (segment.kind == BookControlKind::menu_end) {
        if (!in_menu) return fail("menu end without menu start");
        in_menu = false;
        continue;
      }
      if (segment.kind != BookControlKind::menu_item) continue;
      if (!in_menu || segment.malformed || segment.source_tokens.empty())
        return fail("menu item is outside a valid menu");
      const auto target = normalized(range_text(record, segment.operand_range));
      const auto payload = normalized(range_text(record, segment.payload_range));
      const auto* canonical = find_title(topic_titles, target);
      if (target.empty() || payload.empty() || canonical == nullptr)
        return fail("menu item target or canonical title is unavailable: " +
                    target);
      MenuItemIR item;
      item.logical_record = record.logical_record;
      item.segment_index = segment.segment_index;
      item.target = target;
      item.target_output = segment.operand_range;
      item.label_output = segment.payload_range;
      item.target_cells = source_cells(record, segment.operand_range);
      item.label_cells = source_cells(record, segment.payload_range);
      if (item.target_cells.empty() || item.label_cells.empty())
        return fail("menu item target or label has no exact source cells: " +
                    target);
      const auto canonical_text = normalized(*canonical);
      item.text = payload;
      if (!ascii_equals_case_insensitive(payload, canonical_text)) {
        const auto payload_tokens = source_tokens_intersecting_output(
            record.assembled, segment.payload_range.begin,
            segment.payload_range.end);
        if (payload_tokens.empty())
          return fail("menu item payload has no source tokens: " + target);
        const auto token = payload_tokens.back();
        if (token >= record.encoded_tokens.size() ||
            record.encoded_tokens[token].width != 1)
          return fail("menu item suffix is not a width-1 source token: " +
                      target);
        const auto terminal = visible_token(record, token);
        if (terminal.empty() || terminal.size() > 3 ||
            record.ir.tokens[token].has_spacing_control ||
            payload.size() <= terminal.size() ||
            !ascii_equals_case_insensitive(
                normalized(payload.substr(0, payload.size() - terminal.size())),
                canonical_text) ||
            !ascii_equals_case_insensitive(
                payload.substr(payload.size() - terminal.size()), terminal))
          return fail("menu item differs from its canonical title by more "
                      "than its final source token: " + target +
                      " payload='" + payload + "' canonical='" +
                      canonical_text +
                      "' token='" + terminal + "'");
        item.text = normalized(
            payload.substr(0, payload.size() - terminal.size()));
        item.terminal_marker_token = token;
        item.terminal_marker_encoded = record.ir.tokens[token].encoded;
        item.terminal_marker_bytes = record.ir.tokens[token].byte_range;
        item.terminal_marker_display_cells = terminal.size();
      }
      menu.items.push_back(std::move(item));
    }
  }
  if (!saw_menu || in_menu || menu.items.empty())
    return fail("source does not contain one complete non-empty menu");
  if (error != nullptr) error->clear();
  return menu;
}

bool verify_menu_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const std::map<std::string, std::string>& topic_titles,
    const MenuIR& menu, std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const auto canonical = extract_menu_ir(records, topic_titles);
  if (!canonical) return fail("source does not admit a canonical menu");
  if (canonical->items.size() != menu.items.size())
    return fail("menu item count differs from canonical source lowering");
  for (std::size_t index = 0; index < menu.items.size(); ++index) {
    const auto& actual = menu.items[index];
    const auto& expected = canonical->items[index];
    if (actual.logical_record != expected.logical_record ||
        actual.segment_index != expected.segment_index ||
        actual.target != expected.target || actual.text != expected.text ||
        actual.target_output.begin != expected.target_output.begin ||
        actual.target_output.end != expected.target_output.end ||
        actual.label_output.begin != expected.label_output.begin ||
        actual.label_output.end != expected.label_output.end ||
        !same_cells(actual.target_cells, expected.target_cells) ||
        !same_cells(actual.label_cells, expected.label_cells) ||
        actual.terminal_marker_token != expected.terminal_marker_token ||
        actual.terminal_marker_encoded.has_value() !=
            expected.terminal_marker_encoded.has_value() ||
        (actual.terminal_marker_encoded &&
         !(actual.terminal_marker_encoded.value() ==
           expected.terminal_marker_encoded.value())) ||
        actual.terminal_marker_bytes.has_value() !=
            expected.terminal_marker_bytes.has_value() ||
        (actual.terminal_marker_bytes &&
         (actual.terminal_marker_bytes->begin !=
              expected.terminal_marker_bytes->begin ||
          actual.terminal_marker_bytes->end !=
              expected.terminal_marker_bytes->end)) ||
        actual.terminal_marker_display_cells !=
            expected.terminal_marker_display_cells)
      return fail("menu item text or provenance differs from source");
  }
  if (error != nullptr) error->clear();
  return true;
}

std::string format_menu_ir(const MenuIR& menu) {
  std::ostringstream output;
  for (std::size_t index = 0; index < menu.items.size(); ++index) {
    const auto& item = menu.items[index];
    output << "menu_item=" << index << " record=" << item.logical_record
           << " segment=" << item.segment_index << " target='" << item.target
           << "' text='" << item.text << "' target_cells="
           << item.target_cells.size() << " label_cells="
           << item.label_cells.size();
    if (item.terminal_marker_token) {
      output << " terminal_marker_token=" << *item.terminal_marker_token;
      if (item.terminal_marker_encoded)
        output << " terminal_marker_encoded=0x" << std::hex
               << item.terminal_marker_encoded->value << std::dec
               << " width="
               << static_cast<unsigned>(item.terminal_marker_encoded->width);
      if (item.terminal_marker_bytes)
        output << " terminal_marker_bytes=[0x" << std::hex
               << item.terminal_marker_bytes->begin << ",0x"
               << item.terminal_marker_bytes->end << ')' << std::dec;
      if (item.terminal_marker_display_cells)
        output << " marker_cells=" << *item.terminal_marker_display_cells;
    }
    output << '\n';
  }
  return output.str();
}

} // namespace geist::detail
