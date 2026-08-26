#include "geist/detail/publication_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
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

std::string compose_row(
    const std::vector<DecodedLogicalRecordSource>& records,
    const PhysicalRowIR& row,
    const std::set<std::uint16_t>& marker_identities) {
  const auto* record = find_record(records, row.logical_record);
  if (record == nullptr || row.token_end > record->tokens.size()) return {};
  auto visible = row.visible_text;
  if (row.token_begin < row.token_end) {
    const auto token = row.token_end - 1;
    if (token < record->encoded_tokens.size() &&
        record->encoded_tokens[token].width == 1 &&
        marker_identities.count(record->encoded_tokens[token].value) != 0) {
      auto terminal = record->tokens[token];
      if (!terminal.empty() && terminal.front() < 4)
        terminal.erase(terminal.begin());
      terminal.erase(std::remove(terminal.begin(), terminal.end(), 0x2666),
                     terminal.end());
      const auto terminal_text = token_words_to_ascii(terminal);
      if (!terminal_text.empty() && visible.size() >= terminal_text.size() &&
          visible.compare(visible.size() - terminal_text.size(),
                          terminal_text.size(), terminal_text) == 0)
        visible.erase(visible.size() - terminal_text.size());
    }
  }
  return trim_layout(std::move(visible));
}

bool lexical_marker(const PhysicalRowIR& row) {
  if (!row.marker || row.marker->decoded_text.size() < 2) return false;
  return std::all_of(row.marker->decoded_text.begin(),
                     row.marker->decoded_text.end(), [](unsigned char ch) {
                       return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
                     });
}

std::size_t wide_gap(const std::string& text) {
  std::size_t best = std::string::npos;
  std::size_t best_size = 0;
  for (std::size_t begin = 0; begin < text.size();) {
    if (text[begin] != ' ') {
      ++begin;
      continue;
    }
    auto end = begin;
    while (end < text.size() && text[end] == ' ') ++end;
    if (end - begin >= 10 && end - begin > best_size) {
      best = begin;
      best_size = end - begin;
    }
    begin = end;
  }
  return best;
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
      } else if (saw_font && segment.kind != BookControlKind::text) {
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

  // Every printable source cell in a font segment must be classified by a
  // control or physical row. This is the all-or-nothing admission gate.
  for (const auto& run : layout.runs) {
    if (run.control_kind != BookControlKind::font) continue;
    for (const auto& row : run.rows) {
      const auto* segment = find_segment(records, row);
      if (segment == nullptr) return std::nullopt;
      for (const auto token : segment->source_tokens) {
        for (const auto& cell : ownership.cells) {
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

  std::set<std::uint16_t> marker_identities;
  for (const auto& run : layout.runs)
    for (const auto& row : run.rows)
      if (row.marker) marker_identities.insert(row.marker->encoded_value);

  PublicationCatalogIR catalog;
  catalog.heading_level = heading_level;
  std::vector<std::string> title_rows;
  for (const auto& row : title_run->rows) {
    auto text = compose_row(records, row, marker_identities);
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
    if (!catalog.introduction.empty()) catalog.introduction += ' ';
    catalog.introduction += collapse_ascii_whitespace(title_rows[row]);
  }
  catalog.introduction = collapse_ascii_whitespace(catalog.introduction);
  if (catalog.title.empty()) return std::nullopt;

  for (const auto* run : font_runs) {
    std::map<std::uint16_t, std::size_t> marker_counts;
    for (const auto& row : run->rows)
      if (lexical_marker(row) && row.native_origin == 3 &&
          row.start == PhysicalRowStartKind::explicit_marker_slot)
        ++marker_counts[row.marker->encoded_value];
    PublicationEntryIR* entry = nullptr;
    for (std::size_t row_index = 0; row_index < run->rows.size(); ++row_index) {
      const auto& row = run->rows[row_index];
      auto text = collapse_ascii_whitespace(
          compose_row(records, row, marker_identities));
      if (text.empty()) continue;
      const auto repeated_start =
          lexical_marker(row) && row.native_origin == 3 &&
          row.start == PhysicalRowStartKind::explicit_marker_slot &&
          marker_counts[row.marker->encoded_value] >= 2;
      if (entry == nullptr || repeated_start) {
        catalog.entries.push_back({});
        entry = &catalog.entries.back();
      } else if (!entry->text.empty()) {
        entry->text += ' ';
      }
      entry->text += text;
      entry->source_rows.push_back({run->id, row_index});
    }
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
        catalog.entries[index].source_rows !=
            canonical->entries[index].source_rows)
      return fail("publication entry text or provenance differs from source");
    if (catalog.entries[index].text.empty() ||
        catalog.entries[index].source_rows.empty())
      return fail("publication entry has no text or source provenance");
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
  }
  return output.str();
}

} // namespace geist::detail
