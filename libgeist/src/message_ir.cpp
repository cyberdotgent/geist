#include "geist/detail/message_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace geist::detail {
namespace {

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

std::string first_word(std::string value) {
  value = trim_ascii(std::move(value));
  const auto end = value.find_first_of(" \t\r\n");
  return value.substr(0, end);
}

bool numeric_id_part(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

bool numeric_message_id(const std::string& value) {
  if (numeric_id_part(value)) return true;
  const auto hyphen = value.find('-');
  return hyphen != std::string::npos &&
         value.find('-', hyphen + 1) == std::string::npos &&
         numeric_id_part(value.substr(0, hyphen)) &&
         numeric_id_part(value.substr(hyphen + 1));
}

std::optional<MessageSectionKind> section_kind(std::string value) {
  value = collapse_ascii_whitespace(trim_ascii(std::move(value)));
  // Lower-case single words carried in an SRMSG payload are compression
  // continuations from the preceding prose, not section labels. Canonical
  // labels in this catalog begin with an upper-case source character.
  if (value.empty() ||
      std::isupper(static_cast<unsigned char>(value.front())) == 0)
    return std::nullopt;
  value = ascii_lower(std::move(value));
  const auto matches = [&](const std::string& label) {
    return value == label || value == label + ":" ||
           ascii_starts_with_case_insensitive(value, label + ": ");
  };
  if (matches("meaning")) return MessageSectionKind::meaning;
  if (matches("action")) return MessageSectionKind::action;
  return std::nullopt;
}

bool section_equal(const MessageSectionIR& left,
                   const MessageSectionIR& right) {
  return left.kind == right.kind && left.run == right.run &&
         left.row == right.row &&
         left.logical_record == right.logical_record &&
         left.segment_index == right.segment_index &&
         left.recovered_record_continuation ==
             right.recovered_record_continuation;
}

bool entry_equal(const MessageEntryIR& left, const MessageEntryIR& right) {
  if (left.id != right.id || left.logical_record != right.logical_record ||
      left.segment_index != right.segment_index ||
      left.sections.size() != right.sections.size())
    return false;
  for (std::size_t index = 0; index < left.sections.size(); ++index)
    if (!section_equal(left.sections[index], right.sections[index]))
      return false;
  return true;
}

const char* section_name(MessageSectionKind kind) {
  return kind == MessageSectionKind::meaning ? "meaning" : "action";
}

} // namespace

std::optional<MessageCatalogIR> extract_message_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership, std::string* error) {
  const auto fail =
      [&](const std::string& message) -> std::optional<MessageCatalogIR> {
    if (error != nullptr) *error = message;
    return std::nullopt;
  };
  if (records.empty() || !ownership.conflicts.empty())
    return fail("source ownership is unavailable or conflicted");

  using SegmentKey = std::pair<std::uint32_t, std::size_t>;
  std::map<SegmentKey, std::size_t> entry_by_segment;
  MessageCatalogIR catalog;
  std::optional<std::size_t> active;
  bool saw_nonnumeric_message = false;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
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
          catalog.entries.push_back(
              {id, record.logical_record, segment.segment_index, {}});
          active = catalog.entries.size() - 1;
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

  for (const auto& run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto owner =
          entry_by_segment.find({row.logical_record, row.segment_index});
      if (owner == entry_by_segment.end()) continue;
      const auto kind = section_kind(row.visible_text);
      if (!kind) continue;
      auto& sections = catalog.entries[owner->second].sections;
      if (std::any_of(sections.begin(), sections.end(), [&](const auto& section) {
            return section.kind == *kind;
          }))
        return fail("message entry has a duplicate semantic section: " +
                    catalog.entries[owner->second].id);
      sections.push_back({*kind, run.id, row_index, row.logical_record,
                          row.segment_index, false});
    }
  }

  for (auto& entry : catalog.entries) {
    if (entry.sections.size() == 1 &&
        entry.sections.front().kind == MessageSectionKind::action) {
      const auto& action = entry.sections.front();
      const auto source = std::find_if(
          records.begin(), records.end(), [&](const auto& record) {
            return record.logical_record == action.logical_record;
          });
      const auto text = source == records.end()
                            ? std::string{}
                            : trim_ascii(token_words_to_ascii(
                                  source->assembled.words));
      if (source != records.end() && action.segment_index > 0 &&
          ascii_starts_with_case_insensitive(text, "meaning:")) {
        entry.sections.insert(entry.sections.begin(),
                              {MessageSectionKind::meaning, 0, 0,
                               action.logical_record, 0, true});
      }
    } else if (entry.sections.size() == 1 &&
               entry.sections.front().kind == MessageSectionKind::meaning) {
      const auto& meaning = entry.sections.front();
      auto source = std::find_if(
          records.begin(), records.end(), [&](const auto& record) {
            return record.logical_record > meaning.logical_record;
          });
      for (; source != records.end(); ++source) {
        const auto starts_next_message = std::any_of(
            source->control_segments.begin(), source->control_segments.end(),
            [](const auto& segment) {
              return segment.kind == BookControlKind::message_start;
            });
        const auto text =
            trim_ascii(token_words_to_ascii(source->assembled.words));
        if (ascii_starts_with_case_insensitive(text, "action:")) break;
        if (starts_next_message) {
          source = records.end();
          break;
        }
      }
      if (source != records.end()) {
        entry.sections.push_back({MessageSectionKind::action, 0, 0,
                                  source->logical_record, 0, true});
      }
    }
    if (entry.sections.size() != 2 ||
        entry.sections[0].kind != MessageSectionKind::meaning ||
        entry.sections[1].kind != MessageSectionKind::action)
      return fail("message entry lacks ordered Meaning/Action sections: " +
                  entry.id);
  }
  if (error != nullptr) error->clear();
  return catalog;
}

bool verify_message_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const MessageCatalogIR& catalog, std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const auto canonical = extract_message_catalog_ir(records, layout, ownership);
  if (!canonical)
    return fail("source does not admit a canonical numeric message catalog");
  if (canonical->entries.size() != catalog.entries.size())
    return fail("message entry count differs from canonical lowering");
  for (std::size_t index = 0; index < catalog.entries.size(); ++index)
    if (!entry_equal(canonical->entries[index], catalog.entries[index]))
      return fail("message entry differs from canonical lowering: " +
                  catalog.entries[index].id);
  if (error != nullptr) error->clear();
  return true;
}

std::string format_message_catalog_ir(const MessageCatalogIR& catalog) {
  std::ostringstream out;
  out << "message_catalog entries=" << catalog.entries.size() << '\n';
  for (const auto& entry : catalog.entries) {
    out << "message id='" << entry.id << "' source=" << entry.logical_record
        << ':' << entry.segment_index;
    for (const auto& section : entry.sections) {
      out << ' ' << section_name(section.kind) << '=' << section.run << ':'
          << section.row << '@' << section.logical_record << ':'
          << section.segment_index;
      if (section.recovered_record_continuation) out << "(continuation)";
    }
    out << '\n';
  }
  return out.str();
}

} // namespace geist::detail
