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
             right.recovered_record_continuation &&
         left.label_source_rows == right.label_source_rows &&
         left.source_rows == right.source_rows &&
         left.paragraphs.size() == right.paragraphs.size() &&
         std::equal(left.paragraphs.begin(), left.paragraphs.end(),
                    right.paragraphs.begin(), [](const auto& a, const auto& b) {
                      return a.text == b.text && a.source_rows == b.source_rows &&
                             a.source_segments == b.source_segments;
                    });
}

bool entry_equal(const MessageEntryIR& left, const MessageEntryIR& right) {
  if (left.id != right.id || left.logical_record != right.logical_record ||
      left.segment_index != right.segment_index ||
      left.headline.text != right.headline.text ||
      left.headline.source_rows != right.headline.source_rows ||
      left.headline.source_segments != right.headline.source_segments ||
      left.body.size() != right.body.size() ||
      left.sections.size() != right.sections.size())
    return false;
  for (std::size_t index = 0; index < left.body.size(); ++index)
    if (left.body[index].text != right.body[index].text ||
        left.body[index].source_rows != right.body[index].source_rows ||
        left.body[index].source_segments != right.body[index].source_segments)
      return false;
  if (left.source_rows != right.source_rows ||
      left.suppressed_source_rows != right.suppressed_source_rows)
    return false;
  for (std::size_t index = 0; index < left.sections.size(); ++index)
    if (!section_equal(left.sections[index], right.sections[index]))
      return false;
  return true;
}

const char* section_name(MessageSectionKind kind) {
  return kind == MessageSectionKind::meaning ? "meaning" : "action";
}

const DecodedLogicalRecordSource* find_record(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::uint32_t logical_record) {
  const auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto& record) {
    return record.logical_record == logical_record;
  });
  return found == records.end() ? nullptr : &*found;
}

std::string compose_row_text(
    const std::vector<DecodedLogicalRecordSource>& records,
    const PhysicalRowIR& row, std::string* preceding) {
  if (row.start == PhysicalRowStartKind::record_continuation) {
    const auto* record = find_record(records, row.logical_record);
    if (record != nullptr && row.segment_index < record->control_segments.size()) {
      const auto& segment = record->control_segments[row.segment_index];
      auto text = collapse_ascii_whitespace(
          trim_ascii(range_text(*record, segment.payload_range)));
      if (text.empty() && row.segment_index == 0 &&
          record->control_segments.size() > 1) {
        const auto raw = token_words_to_ascii(record->assembled.words);
        text = collapse_ascii_whitespace(trim_ascii(raw.substr(
            0, record->control_segments[1].complete.begin)));
      }
      if (!text.empty()) return text;
    }
  }
  auto text = collapse_ascii_whitespace(trim_ascii(row.visible_text));
  if (row.marker && !row.marker->decoded_text.empty()) {
    const auto marker = collapse_ascii_whitespace(row.marker->decoded_text);
    const auto lexical = !marker.empty() &&
        std::all_of(marker.begin(), marker.end(), [](const unsigned char ch) {
          return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
        });
    if (lexical && marker != "AN")
      text = marker + (text.empty() ? std::string{} : std::string{" "}) + text;
    else if (marker == "." && preceding != nullptr && !preceding->empty() &&
             preceding->back() != '.')
      *preceding += '.';
  }
  return text;
}

void append_text(std::string& destination, const std::string& text) {
  if (text.empty()) return;
  if (!destination.empty()) destination += ' ';
  destination += text;
}

MessageParagraphIR paragraph_for_segment(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, std::uint32_t logical_record,
    std::size_t segment_index) {
  MessageParagraphIR paragraph;
  for (const auto& run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      if (row.logical_record != logical_record ||
          row.segment_index != segment_index)
        continue;
      auto text = compose_row_text(records, row, &paragraph.text);
      append_text(paragraph.text, text);
      paragraph.source_rows.push_back({run.id, row_index});
      const auto segment = std::make_pair(row.logical_record, row.segment_index);
      if (paragraph.source_segments.empty() ||
          paragraph.source_segments.back() != segment)
        paragraph.source_segments.push_back(segment);
    }
  }
  return paragraph;
}

MessageParagraphIR paragraph_before_segment(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, std::uint32_t logical_record,
    std::size_t segment_index) {
  MessageParagraphIR paragraph;
  for (const auto& run : layout.runs) {
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      if (row.logical_record != logical_record ||
          row.segment_index >= segment_index)
        continue;
      auto text = compose_row_text(records, row, &paragraph.text);
      append_text(paragraph.text, text);
      paragraph.source_rows.push_back({run.id, row_index});
      const auto segment = std::make_pair(row.logical_record, row.segment_index);
      if (paragraph.source_segments.empty() ||
          paragraph.source_segments.back() != segment)
        paragraph.source_segments.push_back(segment);
    }
  }
  return paragraph;
}

void remove_body_rows(MessageEntryIR& entry,
                      const std::vector<MessageSourceRowIR>& rows) {
  entry.body.erase(
      std::remove_if(entry.body.begin(), entry.body.end(), [&](const auto& body) {
        return body.source_rows == rows;
      }),
      entry.body.end());
}

void remove_section_rows(MessageSectionIR& section,
                         const std::vector<MessageSourceRowIR>& rows) {
  const auto remove = [&](auto& values) {
    values.erase(std::remove_if(values.begin(), values.end(), [&](const auto& value) {
                   return std::find(rows.begin(), rows.end(), value) != rows.end();
                 }),
                 values.end());
  };
  remove(section.source_rows);
  for (auto& paragraph : section.paragraphs) remove(paragraph.source_rows);
}

std::string section_payload(MessageSectionKind kind, std::string text) {
  text = collapse_ascii_whitespace(trim_ascii(std::move(text)));
  const auto label = std::string(section_name(kind));
  if (!ascii_starts_with_case_insensitive(text, label)) return text;
  auto begin = label.size();
  if (begin < text.size() && text[begin] == ':') ++begin;
  return trim_ascii(text.substr(begin));
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

  std::vector<std::optional<std::size_t>> active_section(catalog.entries.size());
  for (const auto& run : layout.runs) {
    std::optional<std::size_t> run_owner;
    for (const auto& row : run.rows) {
      const auto owner = entry_by_segment.find(
          {row.logical_record, row.segment_index});
      if (owner != entry_by_segment.end()) {
        if (run_owner && *run_owner != owner->second)
          return fail("one display run crosses message entry ownership");
        run_owner = owner->second;
      }
    }
    if (!run_owner) continue;
    auto& entry = catalog.entries[*run_owner];
    if (run.control_kind == BookControlKind::structural ||
        run.control_kind == BookControlKind::unknown) {
      for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
        const MessageSourceRowIR source{run.id, row_index};
        entry.source_rows.push_back(source);
        entry.suppressed_source_rows.push_back(source);
      }
      continue;
    }

    std::optional<MessageSectionKind> run_kind;
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto owner =
          entry_by_segment.find({row.logical_record, row.segment_index});
      if (owner == entry_by_segment.end()) continue;
      const auto kind = section_kind(row.visible_text);
      if (kind) {
        if (run_kind && *run_kind != *kind)
          return fail("one display run contains two message sections");
        run_kind = kind;
      }
    }
    if (run_kind) {
      auto& sections = entry.sections;
      if (std::any_of(sections.begin(), sections.end(), [&](const auto& section) {
            return section.kind == *run_kind;
          }))
        return fail("message entry has a duplicate semantic section: " +
                    entry.id);
      const auto label_row = std::find_if(
          run.rows.begin(), run.rows.end(), [&](const auto& row) {
            const auto kind = section_kind(row.visible_text);
            return kind && *kind == *run_kind;
          });
      const auto row_index = static_cast<std::size_t>(
          std::distance(run.rows.begin(), label_row));
      MessageSectionIR section;
      section.kind = *run_kind;
      section.run = run.id;
      section.row = row_index;
      section.logical_record = label_row->logical_record;
      section.segment_index = label_row->segment_index;
      section.label_source_rows.push_back({run.id, row_index});
      section.paragraphs.push_back({});
      entry.sections.push_back(std::move(section));
      active_section[*run_owner] = entry.sections.size() - 1;
    }

    MessageParagraphIR run_paragraph;
    for (std::size_t row_index = 0; row_index < run.rows.size(); ++row_index) {
      const auto& row = run.rows[row_index];
      const auto owner = entry_by_segment.find(
          {row.logical_record, row.segment_index});
      if (owner == entry_by_segment.end())
        return fail("message run contains an unowned visible row");
      const MessageSourceRowIR source{run.id, row_index};
      entry.source_rows.push_back(source);
      auto text = compose_row_text(records, row, &run_paragraph.text);
      if (run_kind && section_kind(row.visible_text))
        text = section_payload(*run_kind, std::move(text));
      append_text(run_paragraph.text, text);
      run_paragraph.source_rows.push_back(source);
      const auto segment = std::make_pair(row.logical_record, row.segment_index);
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
      auto& section = entry.sections[*active_section[*run_owner]];
      auto& paragraph = section.paragraphs.back();
      append_text(paragraph.text, run_paragraph.text);
      paragraph.source_rows.insert(paragraph.source_rows.end(),
                                   run_paragraph.source_rows.begin(),
                                   run_paragraph.source_rows.end());
      paragraph.source_segments.insert(paragraph.source_segments.end(),
                                       run_paragraph.source_segments.begin(),
                                       run_paragraph.source_segments.end());
      section.source_rows.insert(section.source_rows.end(),
                                 run_paragraph.source_rows.begin(),
                                 run_paragraph.source_rows.end());
    } else if (entry.headline.source_rows.empty()) {
      entry.headline = std::move(run_paragraph);
    } else {
      entry.body.push_back(std::move(run_paragraph));
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
        auto paragraph = paragraph_before_segment(
            records, layout, action.logical_record, action.segment_index);
        if (paragraph.text.empty()) {
          const auto* source = find_record(records, action.logical_record);
          if (source != nullptr) {
            const auto complete = source->control_segments[action.segment_index]
                                      .complete.begin;
            const auto raw = token_words_to_ascii(source->assembled.words);
            paragraph.text = collapse_ascii_whitespace(
                trim_ascii(raw.substr(0, complete)));
            if (paragraph.text.empty()) {
              auto end = ascii_lower(raw).find("action:");
              if (end == std::string::npos) end = raw.size();
              paragraph.text = collapse_ascii_whitespace(
                  trim_ascii(raw.substr(0, end)));
            }
            paragraph.source_segments.push_back({action.logical_record, 0});
          }
        }
        paragraph.text = section_payload(MessageSectionKind::meaning,
                                         std::move(paragraph.text));
        if (paragraph.text.empty() && !entry.body.empty()) {
          paragraph = std::move(entry.body.back());
          entry.body.pop_back();
          paragraph.text = section_payload(MessageSectionKind::meaning,
                                           std::move(paragraph.text));
        }
        remove_body_rows(entry, paragraph.source_rows);
        MessageSectionIR section;
        section.kind = MessageSectionKind::meaning;
        section.logical_record = action.logical_record;
        section.recovered_record_continuation = true;
        section.source_rows = paragraph.source_rows;
        section.paragraphs.push_back(std::move(paragraph));
        entry.sections.insert(entry.sections.begin(), std::move(section));
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
        auto paragraph = paragraph_for_segment(
            records, layout, source->logical_record, 0);
        if (paragraph.text.empty()) {
          paragraph.text = collapse_ascii_whitespace(trim_ascii(
              range_text(*source, source->control_segments.front().payload_range)));
          paragraph.source_segments.push_back({source->logical_record, 0});
        }
        paragraph.text = section_payload(MessageSectionKind::action,
                                         std::move(paragraph.text));
        MessageSectionIR section;
        section.kind = MessageSectionKind::action;
        section.logical_record = source->logical_record;
        section.recovered_record_continuation = true;
        section.source_rows = paragraph.source_rows;
        section.paragraphs.push_back(std::move(paragraph));
        remove_section_rows(entry.sections.front(), section.source_rows);
        entry.sections.push_back(std::move(section));
      }
    }
    if (entry.sections.size() != 2 ||
        entry.sections[0].kind != MessageSectionKind::meaning ||
        entry.sections[1].kind != MessageSectionKind::action)
      return fail("message entry lacks ordered Meaning/Action sections: " +
                  entry.id);
    if (entry.headline.text.find(entry.id) == std::string::npos) {
      const auto headline = std::find_if(
          entry.body.begin(), entry.body.end(), [&](const auto& paragraph) {
            return paragraph.text.find(entry.id) != std::string::npos;
          });
      const auto fallback = headline != entry.body.end()
                                ? headline
                                : std::find_if(entry.body.begin(),
                                               entry.body.end(),
                                               [](const auto& paragraph) {
                                                 return !paragraph.text.empty();
                                               });
      if (fallback != entry.body.end()) {
        entry.suppressed_source_rows.insert(
            entry.suppressed_source_rows.end(),
            entry.headline.source_rows.begin(), entry.headline.source_rows.end());
        for (auto preceding = entry.body.begin(); preceding != fallback;
             ++preceding)
          entry.suppressed_source_rows.insert(
              entry.suppressed_source_rows.end(),
              preceding->source_rows.begin(), preceding->source_rows.end());
        entry.headline = std::move(*fallback);
        entry.body.erase(entry.body.begin(), std::next(fallback));
      }
    }
    if (entry.headline.text.empty()) {
      const auto* source = find_record(records, entry.logical_record);
      if (source != nullptr) {
        const auto font = std::find_if(
            source->control_segments.begin() +
                static_cast<std::ptrdiff_t>(entry.segment_index + 1),
            source->control_segments.end(), [](const auto& segment) {
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
    for (auto& section : entry.sections) {
      if (!section.paragraphs.empty() && section.paragraphs.front().text.empty()) {
        const auto continuation = std::find_if(
            records.begin(), records.end(), [&](const auto& record) {
              return record.logical_record > section.logical_record;
            });
        if (continuation != records.end()) {
          auto raw = token_words_to_ascii(continuation->assembled.words);
          auto end = ascii_lower(raw).find("cfont");
          if (end != std::string::npos) raw.resize(end);
          while (!raw.empty() &&
                 std::ispunct(static_cast<unsigned char>(raw.front())) != 0)
            raw.erase(raw.begin());
          section.paragraphs.front().text =
              collapse_ascii_whitespace(trim_ascii(std::move(raw)));
          section.paragraphs.front().source_segments.push_back(
              {continuation->logical_record, 0});
        }
      }
      if (section.paragraphs.empty() || section.paragraphs.front().text.empty() ||
          (section.source_rows.empty() &&
           section.paragraphs.front().source_segments.empty())) {
        return fail("message section lacks source-proven text: " + entry.id +
                    " " + section_name(section.kind));
      }
    }
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
        << ':' << entry.segment_index << " headline='" << entry.headline.text
        << "' rows=" << entry.source_rows.size()
        << " suppressed=" << entry.suppressed_source_rows.size();
    for (const auto& section : entry.sections) {
      out << ' ' << section_name(section.kind) << '=' << section.run << ':'
          << section.row << '@' << section.logical_record << ':'
          << section.segment_index;
      if (section.recovered_record_continuation) out << "(continuation)";
      out << " text='";
      for (std::size_t paragraph = 0; paragraph < section.paragraphs.size();
           ++paragraph) {
        if (paragraph != 0) out << " | ";
        out << section.paragraphs[paragraph].text;
      }
      out << '\'';
    }
    out << '\n';
  }
  return out.str();
}

} // namespace geist::detail
