#include "geist/detail/publication_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace geist::detail {
namespace {

const DecodedLogicalRecordSource* find_record(
    const std::vector<DecodedLogicalRecordSource>& records,
    std::uint32_t logical_record) {
  const auto found = std::find_if(records.begin(), records.end(),
                                  [&](const auto& record) {
    return record.logical_record == logical_record;
  });
  return found == records.end() ? nullptr : &*found;
}

const ControlSegmentIR* find_segment(
    const std::vector<DecodedLogicalRecordSource>& records,
    const PhysicalRowIR& row) {
  const auto* record = find_record(records, row.logical_record);
  if (record == nullptr || row.segment_index >= record->control_segments.size())
    return nullptr;
  return &record->control_segments[row.segment_index];
}

std::string range_text(const DecodedLogicalRecordSource& record,
                       const OutputRangeIR& range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin > range.end || range.end > text.size()) return {};
  return text.substr(range.begin, range.end - range.begin);
}

bool all_c_font(const DecodedLogicalRecordSource& record,
                const ControlSegmentIR& segment) {
  auto operands = trim_ascii(range_text(record, segment.operand_range));
  std::vector<std::string> words;
  for (std::size_t begin = 0; begin < operands.size();) {
    while (begin < operands.size() &&
           std::isspace(static_cast<unsigned char>(operands[begin])) != 0)
      ++begin;
    auto end = begin;
    while (end < operands.size() &&
           std::isspace(static_cast<unsigned char>(operands[end])) == 0)
      ++end;
    if (begin < end) words.push_back(operands.substr(begin, end - begin));
    begin = end;
  }
  if (words.empty() || words.size() % 3 != 0) return false;
  for (std::size_t word = 2; word < words.size(); word += 3) {
    if (!ascii_equals_case_insensitive(words[word], "C")) return false;
  }
  return true;
}

std::string trim_layout(std::string value) {
  const auto visible = [](const unsigned char ch) {
    return ch >= 0x20 && std::isspace(ch) == 0;
  };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), visible));
  value.erase(std::find_if(value.rbegin(), value.rend(), visible).base(),
              value.end());
  return value;
}

bool lexical_marker(const PhysicalRowIR& row) {
  if (!row.marker || row.marker->decoded_text.size() < 2) return false;
  return std::all_of(row.marker->decoded_text.begin(),
                     row.marker->decoded_text.end(), [](unsigned char ch) {
                       return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
                     });
}

std::string compose_row(
    const std::vector<DecodedLogicalRecordSource>& records,
    const PhysicalRowIR& row, bool continuation_row, bool final_row) {
  const auto* record = find_record(records, row.logical_record);
  if (record == nullptr || row.token_end > record->tokens.size()) return {};
  auto visible = row.visible_text;
  const auto has_question_padding = std::any_of(
      record->tokens.begin() + static_cast<std::ptrdiff_t>(row.token_begin),
      record->tokens.begin() + static_cast<std::ptrdiff_t>(row.token_end),
      [](const auto& token) {
        return (std::find(token.begin(), token.end(), '?') != token.end() ||
                std::find(token.begin(), token.end(), 0x2666) != token.end()) &&
               std::all_of(token.begin(), token.end(), [](const auto word) {
                 return word == '?' || word == ' ' || word == 0x2666 ||
                        word < 0x20;
               });
      });
  if (has_question_padding) {
    for (auto found = visible.find('?'); found != std::string::npos;
         found = visible.find('?', found)) {
      const auto left_space =
          found == 0 ||
          std::isspace(static_cast<unsigned char>(visible[found - 1])) != 0;
      const auto right_space =
          found + 1 == visible.size() ||
          std::isspace(static_cast<unsigned char>(visible[found + 1])) != 0;
      if (left_space && right_space)
        visible.erase(found, 1);
      else
        ++found;
    }
  }
  if (row.token_begin < row.token_end) {
    const auto token = row.token_end - 1;
    if (token < record->encoded_tokens.size() &&
        record->encoded_tokens[token].width == 1) {
      auto terminal = record->tokens[token];
      const auto compact_attached =
          !terminal.empty() && terminal.front() >= 4;
      if (!terminal.empty() && terminal.front() < 4)
        terminal.erase(terminal.begin());
      terminal.erase(std::remove(terminal.begin(), terminal.end(), 0x2666),
                     terminal.end());
      const auto terminal_text = token_words_to_ascii(terminal);
      if (!terminal_text.empty() && visible.size() >= terminal_text.size() &&
          visible.compare(visible.size() - terminal_text.size(),
                          terminal_text.size(), terminal_text) == 0) {
        const auto prefix_size = visible.size() - terminal_text.size();
        const auto structurally_attached =
            compact_attached && prefix_size != 0 &&
            std::isspace(static_cast<unsigned char>(visible[prefix_size - 1])) ==
                0 &&
            std::isalnum(static_cast<unsigned char>(visible[prefix_size - 1])) ==
                0;
        const auto terminal_run_marker =
            final_row && terminal_text.size() == 1 &&
            std::string("</(").find(terminal_text.front()) !=
                std::string::npos;
        if (structurally_attached || terminal_run_marker)
          visible.erase(prefix_size);
      }
    }
  }
  if (continuation_row && row.native_origin != 3 && lexical_marker(row)) {
    visible = row.marker->decoded_text +
              (visible.empty() ? std::string{} : std::string{" "}) + visible;
  }
  return trim_layout(std::move(visible));
}

bool has_attached_terminal_artifact(
    const std::vector<DecodedLogicalRecordSource>& records,
    const PhysicalRowIR& row) {
  const auto* record = find_record(records, row.logical_record);
  if (record == nullptr || row.token_begin >= row.token_end ||
      row.token_end > record->tokens.size())
    return false;
  const auto token = row.token_end - 1;
  if (token >= record->encoded_tokens.size() ||
      record->encoded_tokens[token].width != 1 ||
      record->tokens[token].empty() || record->tokens[token].front() < 4)
    return false;
  const auto terminal = token_words_to_ascii(record->tokens[token]);
  if (terminal.empty() || row.visible_text.size() <= terminal.size() ||
      row.visible_text.compare(row.visible_text.size() - terminal.size(),
                               terminal.size(), terminal) != 0)
    return false;
  const auto before = static_cast<unsigned char>(
      row.visible_text[row.visible_text.size() - terminal.size() - 1]);
  return std::isspace(before) == 0 && std::isalnum(before) == 0;
}

std::size_t wide_gap(const std::string& text) {
  for (std::size_t begin = 0; begin < text.size();) {
    if (text[begin] != ' ') {
      ++begin;
      continue;
    }
    auto end = begin;
    while (end < text.size() && text[end] == ' ') ++end;
    if (end - begin >= 8) return begin;
    begin = end;
  }
  return std::string::npos;
}

std::vector<std::string> split_wide_fields(const std::string& text) {
  std::vector<std::string> fields;
  const auto gap = wide_gap(text);
  if (gap == std::string::npos) {
    auto field = collapse_ascii_whitespace(text);
    if (!field.empty()) fields.push_back(std::move(field));
    return fields;
  }
  auto first = collapse_ascii_whitespace(text.substr(0, gap));
  if (!first.empty()) fields.push_back(std::move(first));
  auto next = gap;
  while (next < text.size() && text[next] == ' ') ++next;
  auto second = collapse_ascii_whitespace(text.substr(next));
  if (!second.empty()) fields.push_back(std::move(second));
  return fields;
}

bool ends_complete_statement(const std::string& text) {
  if (text.empty()) return true;
  return std::string(".!?:;)]").find(text.back()) != std::string::npos;
}

void append_paragraph_text(PublicationParagraphIR& paragraph,
                           const std::string& text) {
  if (!paragraph.text.empty() &&
      !(paragraph.text.back() == '/' &&
        std::isalnum(static_cast<unsigned char>(text.front())) != 0))
    paragraph.text += ' ';
  paragraph.text += text;
}

} // namespace

std::optional<PublicationCatalogIR> extract_publication_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership) {
  if (records.empty() || !ownership.conflicts.empty()) return std::nullopt;
  const DisplayRunIR* title_run = nullptr;
  std::vector<const DisplayRunIR*> font_runs;
  std::size_t font_segments = 0;
  std::string heading_level;
  bool saw_title = false;
  bool saw_font = false;
  for (const auto& record : records) {
    for (const auto& segment : record.control_segments) {
      if (segment.kind == BookControlKind::heading_level) {
        heading_level = ascii_lower(trim_ascii(range_text(
            record, segment.operand_range)));
        if (!heading_level.empty() && heading_level.front() == ':')
          heading_level.erase(heading_level.begin());
      } else if (segment.kind == BookControlKind::title) {
        if (saw_title || saw_font) return std::nullopt;
        saw_title = true;
      } else if (segment.kind == BookControlKind::font) {
        saw_font = true;
        ++font_segments;
        if (!all_c_font(record, segment)) return std::nullopt;
      } else if (saw_title && !saw_font &&
                 segment.kind == BookControlKind::text) {
        return std::nullopt;
      } else if (saw_font && segment.kind != BookControlKind::text) {
        return std::nullopt;
      } else if (!saw_title &&
                 (segment.kind == BookControlKind::structural ||
                  segment.kind == BookControlKind::unknown ||
                  segment.kind == BookControlKind::menu_start ||
                  segment.kind == BookControlKind::menu_item ||
                  segment.kind == BookControlKind::menu_end ||
                  segment.kind == BookControlKind::message_start ||
                  segment.kind == BookControlKind::select ||
                  segment.kind == BookControlKind::table_start ||
                  segment.kind == BookControlKind::table_end)) {
        return std::nullopt;
      }
    }
  }
  if (!saw_title || font_segments == 0 ||
      (heading_level != "h2" && heading_level != "h3"))
    return std::nullopt;

  for (const auto& run : layout.runs) {
    if (run.control_kind == BookControlKind::title) {
      if (title_run != nullptr) return std::nullopt;
      title_run = &run;
    } else if (run.control_kind == BookControlKind::font) {
      font_runs.push_back(&run);
    }
  }
  if (title_run == nullptr || font_runs.size() != font_segments)
    return std::nullopt;
  for (const auto& run : layout.runs) {
    if (run.control_kind != BookControlKind::title &&
        run.control_kind != BookControlKind::font)
      return std::nullopt;
    if (run.control_kind != BookControlKind::font) continue;
    if (run.rows.empty() ||
        run.rows.front().start == PhysicalRowStartKind::placeholder_wrap ||
        (run.rows.front().start == PhysicalRowStartKind::control_payload &&
         run.rows.front().native_origin <= 3))
      return std::nullopt;
    if (run.rows.front().marker &&
        (run.rows.front().marker->decoded_text == "[" ||
         run.rows.front().marker->decoded_text == "]"))
      return std::nullopt;
  }
  if (font_runs.size() == 1 && font_runs.front()->rows.size() != 1)
    return std::nullopt;

  // Every printable source cell in a font segment must be classified by a
  // control or physical row. This is the all-or-nothing admission gate.
  std::map<std::pair<std::uint32_t, std::size_t>,
           std::vector<const OwnedSourceCellIR*>>
      cells_by_token;
  for (const auto& cell : ownership.cells)
    cells_by_token[{cell.logical_record, cell.token_index}].push_back(&cell);
  for (const auto& run : layout.runs) {
    if (run.control_kind != BookControlKind::font) continue;
    for (const auto& row : run.rows) {
      const auto* segment = find_segment(records, row);
      if (segment == nullptr) return std::nullopt;
      for (const auto token : segment->source_tokens) {
        const auto found = cells_by_token.find({row.logical_record, token});
        if (found == cells_by_token.end()) return std::nullopt;
        for (const auto* owned : found->second) {
          const auto& cell = *owned;
          const auto ascii_space =
              cell.word <= 0xff &&
              std::isspace(static_cast<unsigned char>(cell.word)) != 0;
          if (cell.logical_record == row.logical_record &&
              cell.token_index == token && cell.word >= 0x20 &&
              cell.word != 0x2666 && !ascii_space &&
              cell.disposition == SourceDisposition::opaque)
            return std::nullopt;
        }
      }
      break;
    }
  }

  PublicationCatalogIR catalog;
  catalog.heading_level = heading_level;
  std::vector<std::string> title_rows;
  for (std::size_t row_index = 0; row_index < title_run->rows.size();
       ++row_index) {
    auto text = compose_row(records, title_run->rows[row_index],
                            row_index != 0,
                            row_index + 1 == title_run->rows.size());
    if (!text.empty()) title_rows.push_back(std::move(text));
  }
  if (title_rows.empty()) return std::nullopt;
  const auto gap = wide_gap(title_rows.front());
  if (gap == std::string::npos) {
    catalog.title = collapse_ascii_whitespace(title_rows.front());
  } else {
    catalog.title = collapse_ascii_whitespace(title_rows.front().substr(0, gap));
    catalog.introduction =
        collapse_ascii_whitespace(title_rows.front().substr(gap));
  }
  for (std::size_t row = 1; row < title_rows.size(); ++row) {
    const auto prefix_entry =
        row >= 2 &&
        title_run->rows[row].start != PhysicalRowStartKind::placeholder_wrap &&
        title_run->rows[row].start != PhysicalRowStartKind::record_continuation;
    if (prefix_entry) {
      PublicationEntryIR entry;
      entry.text = collapse_ascii_whitespace(title_rows[row]);
      entry.source_rows.push_back({title_run->id, row});
      entry.paragraphs.push_back({entry.text, entry.source_rows});
      if (!entry.text.empty()) catalog.entries.push_back(std::move(entry));
    } else {
      if (!catalog.introduction.empty()) catalog.introduction += ' ';
      catalog.introduction += collapse_ascii_whitespace(title_rows[row]);
    }
  }
  catalog.introduction = collapse_ascii_whitespace(catalog.introduction);
  if (catalog.title.empty() || catalog.introduction.empty())
    return std::nullopt;

  const DisplayRunIR* previous_font_run = nullptr;
  for (const auto* run : font_runs) {
    std::map<std::uint16_t, std::size_t> marker_counts;
    for (const auto& row : run->rows)
      if (lexical_marker(row) && row.native_origin == 3 &&
          row.start == PhysicalRowStartKind::explicit_marker_slot)
        ++marker_counts[row.marker->encoded_value];
    const auto previous_text =
        !catalog.entries.empty() && !catalog.entries.back().paragraphs.empty()
            ? catalog.entries.back().paragraphs.back().text
            : std::string{};
    const auto continues_previous =
        previous_font_run != nullptr && !catalog.entries.empty() &&
        !run->rows.empty() && !previous_font_run->rows.empty() &&
        wide_gap(run->rows.front().visible_text) != std::string::npos &&
        !ends_complete_statement(previous_text) &&
        !has_attached_terminal_artifact(
            records, previous_font_run->rows.back());
    PublicationEntryIR* entry =
        continues_previous ? &catalog.entries.back() : nullptr;
    for (std::size_t row_index = 0; row_index < run->rows.size(); ++row_index) {
      const auto& row = run->rows[row_index];
      const auto fields =
          split_wide_fields(compose_row(records, row, row_index != 0,
                                        row_index + 1 == run->rows.size()));
      if (fields.empty()) continue;
      const auto repeated_start =
          lexical_marker(row) && row.native_origin == 3 &&
          row.start == PhysicalRowStartKind::explicit_marker_slot &&
          marker_counts[row.marker->encoded_value] >= 2;
      if (entry == nullptr || repeated_start) {
        catalog.entries.push_back({});
        entry = &catalog.entries.back();
      }
      if (!entry->paragraphs.empty() && row.marker &&
          row.start == PhysicalRowStartKind::explicit_marker_slot &&
          row.marker->decoded_text.size() == 1 &&
          std::string(".,;:!?").find(row.marker->decoded_text.front()) !=
              std::string::npos &&
          entry->paragraphs.back().text.back() !=
              row.marker->decoded_text.front())
        entry->paragraphs.back().text += row.marker->decoded_text;
      entry->source_rows.push_back({run->id, row_index});
      for (std::size_t field_index = 0; field_index < fields.size();
           ++field_index) {
        const auto aligned_parenthetical = fields[field_index].front() == '(';
        const auto title_boundary =
            field_index == 0 && !entry->paragraphs.empty() &&
            entry->paragraphs.back().text.back() == ')' &&
            !(continues_previous && row_index == 0);
        const auto aligned_wrap =
            field_index != 0 &&
            (!ends_complete_statement(fields[field_index - 1]) ||
             aligned_parenthetical ||
             std::isdigit(
                 static_cast<unsigned char>(fields[field_index].front())) != 0);
        if (entry->paragraphs.empty() || title_boundary ||
            (field_index != 0 && !aligned_wrap))
          entry->paragraphs.push_back({});
        auto& paragraph = entry->paragraphs.back();
        append_paragraph_text(paragraph, fields[field_index]);
        paragraph.source_rows.push_back({run->id, row_index});
      }
    }
    previous_font_run = run;
  }
  for (auto& entry : catalog.entries) {
    entry.text.clear();
    for (const auto& paragraph : entry.paragraphs) {
      if (!entry.text.empty()) entry.text += ' ';
      entry.text += paragraph.text;
    }
    if (entry.paragraphs.size() > 2) return std::nullopt;
    if (entry.text.find("://") != std::string::npos) return std::nullopt;
  }
  catalog.entries.erase(
      std::remove_if(catalog.entries.begin(), catalog.entries.end(),
                     [](const auto& entry) { return entry.text.empty(); }),
      catalog.entries.end());
  return catalog.entries.empty() ? std::nullopt
                                 : std::optional<PublicationCatalogIR>(
                                       std::move(catalog));
}

bool verify_publication_catalog_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const LayoutIR& layout, const OwnershipIR& ownership,
    const PublicationCatalogIR& catalog, std::string* error) {
  const auto fail = [&](const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
  };
  const auto canonical =
      extract_publication_catalog_ir(records, layout, ownership);
  if (!canonical) return fail("source does not admit a publication catalog");
  if (canonical->heading_level != catalog.heading_level ||
      canonical->title != catalog.title ||
      canonical->introduction != catalog.introduction ||
      canonical->entries.size() != catalog.entries.size())
    return fail("publication catalog differs from canonical source lowering");
  for (std::size_t index = 0; index < catalog.entries.size(); ++index) {
    if (catalog.entries[index].text != canonical->entries[index].text ||
        catalog.entries[index].paragraphs.size() !=
            canonical->entries[index].paragraphs.size() ||
        catalog.entries[index].source_rows !=
            canonical->entries[index].source_rows)
      return fail("publication entry text or provenance differs from source");
    if (catalog.entries[index].text.empty() ||
        catalog.entries[index].paragraphs.empty() ||
        catalog.entries[index].source_rows.empty())
      return fail("publication entry has no text or source provenance");
    for (std::size_t paragraph = 0;
         paragraph < catalog.entries[index].paragraphs.size(); ++paragraph) {
      const auto& actual = catalog.entries[index].paragraphs[paragraph];
      const auto& expected = canonical->entries[index].paragraphs[paragraph];
      if (actual.text != expected.text ||
          actual.source_rows != expected.source_rows || actual.text.empty() ||
          actual.source_rows.empty())
        return fail("publication paragraph differs from source provenance");
    }
  }
  if (error != nullptr) error->clear();
  return true;
}

std::string format_publication_catalog_ir(
    const PublicationCatalogIR& catalog) {
  std::ostringstream output;
  output << "publication heading=" << catalog.heading_level << " title='"
         << catalog.title << "' introduction='" << catalog.introduction
         << "'\n";
  for (std::size_t index = 0; index < catalog.entries.size(); ++index) {
    output << "entry=" << index << " text='" << catalog.entries[index].text
           << "' sources=";
    for (const auto& source : catalog.entries[index].source_rows)
      output << source.first << ':' << source.second << ',';
    output << '\n';
    for (std::size_t paragraph = 0;
         paragraph < catalog.entries[index].paragraphs.size(); ++paragraph) {
      output << "paragraph=" << index << ':' << paragraph << " text='"
             << catalog.entries[index].paragraphs[paragraph].text
             << "' sources=";
      for (const auto& source :
           catalog.entries[index].paragraphs[paragraph].source_rows)
        output << source.first << ':' << source.second << ',';
      output << '\n';
    }
  }
  return output.str();
}

} // namespace geist::detail
