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
      left.introductions.size() != right.introductions.size() ||
      left.anchor.has_value() != right.anchor.has_value() ||
      left.items.size() != right.items.size() ||
      left.segments.size() != right.segments.size())
    return false;
  for (std::size_t index = 0; index < left.introductions.size(); ++index) {
    const auto &a = left.introductions[index];
    const auto &b = right.introductions[index];
    if (a.text != b.text || !same_slice(a.source, b.source) ||
        !same_cells(a.cells, b.cells))
      return false;
  }
  if (left.anchor && (left.anchor->id != right.anchor->id ||
                      !same_slice(left.anchor->source, right.anchor->source)))
    return false;
  for (std::size_t index = 0; index < left.items.size(); ++index) {
    const auto &a = left.items[index];
    const auto &b = right.items[index];
    if (a.target.kind != b.target.kind || a.target.value != b.target.value ||
        a.label != b.label || !same_slice(a.source, b.source) ||
        !same_cells(a.target_cells, b.target_cells) ||
        !same_cells(a.label_cells, b.label_cells) ||
        !same_cells(a.marker_cells, b.marker_cells))
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

std::string cell_text(const std::vector<MenuSourceCellIR> &cells) {
  TokenWords words;
  words.reserve(cells.size());
  for (const auto &cell : cells)
    words.push_back(cell.word);
  return collapse_ascii_whitespace(
      trim_ascii(token_words_to_ascii(words)));
}

bool ascii_space(std::uint16_t word) {
  return word <= 0x7f &&
         std::isspace(static_cast<unsigned char>(word)) != 0;
}

void trim_cells(std::vector<MenuSourceCellIR> &cells) {
  while (!cells.empty() && ascii_space(cells.front().word))
    cells.erase(cells.begin());
  while (!cells.empty() && ascii_space(cells.back().word))
    cells.pop_back();
}

const OwnedSourceCellIR *owned_cell(const OwnershipIR &ownership,
                                    const MenuSourceCellIR &cell) {
  const auto found = std::find_if(
      ownership.cells.begin(), ownership.cells.end(), [&](const auto &owned) {
        return owned.logical_record == cell.logical_record &&
               owned.token_index == cell.token_index &&
               owned.word_index == cell.word_index;
      });
  return found == ownership.cells.end() ? nullptr : &*found;
}

// Second observed ST title/intro form (SC34-425 1.8.5.5 / 1.8.15.5 /
// 1.8.18.5, LR696 segment 8): the payload occupies two physical rows of one
// display run.  Row 0 (native origin 1) holds the title; row 1 starts at a
// marker slot LayoutIR types as a `?`-run placeholder wrap (token 29, one
// encoded width-1 word shown as eleven U+2500 cells), followed by three
// origin cells (native origin 3) and the introduction prose.  Hosted
// BookServer (DT=19921112160049) shows `1.8.5.5   Example` as the heading
// and `   This example calls the DBUTIL service.` as a separate line
// indented by exactly those origin cells, and never displays the marker.  A
// 50-column title field would have carried the intro in row 0 with a padding
// run; a plain soft wrap of the title keeps the row origin.  The split is
// decided from LayoutIR row membership/origins and OwnershipIR dispositions
// only; no spelling is inspected.  Marker-slot and origin cells are layout
// and belong to neither text.
bool split_title_and_introduction_at_marker_row(
    const std::vector<MenuSourceCellIR> &payload_cells,
    const LayoutIR &layout, const OwnershipIR &ownership,
    std::vector<MenuSourceCellIR> *title,
    std::vector<MenuSourceCellIR> *introduction) {
  const DisplayRunIR *run = nullptr;
  for (const auto &cell : payload_cells) {
    const auto *owned = owned_cell(ownership, cell);
    if (owned == nullptr)
      continue;
    const auto found = std::find_if(layout.runs.begin(), layout.runs.end(),
                                    [&](const auto &candidate) {
                                      return candidate.id == owned->run;
                                    });
    if (found == layout.runs.end() || (run != nullptr && run != &*found))
      return false;
    run = &*found;
  }
  if (run == nullptr || run->rows.size() < 2 ||
      run->rows[0].start != PhysicalRowStartKind::control_payload ||
      run->rows[0].marker.has_value() ||
      run->rows[1].start != PhysicalRowStartKind::placeholder_wrap ||
      run->rows[1].break_before != PhysicalBreakKind::soft_wrap ||
      !run->rows[1].marker.has_value() ||
      run->rows[1].native_origin <= run->rows[0].native_origin)
    return false;
  // Further rows may only continue the introduction as ordinary wraps.
  for (std::size_t row = 2; row < run->rows.size(); ++row)
    if (run->rows[row].start == PhysicalRowStartKind::placeholder_wrap ||
        run->rows[row].start == PhysicalRowStartKind::control_payload)
      return false;

  // Unowned cells are inserted spaces; they follow the preceding owned cell
  // (leading ones precede the title and are trimmed).
  auto current_row = std::size_t{0};
  auto intro_visible = false;
  for (const auto &cell : payload_cells) {
    const auto *owned = owned_cell(ownership, cell);
    if (owned != nullptr) {
      if (owned->row_index < current_row)
        return false;
      current_row = owned->row_index;
    } else if (cell.kind != MenuSourceCellKind::inserted_space) {
      return false;
    }
    if (current_row == 0) {
      // Row 0 carries its own origin cells before the title text.
      if (owned == nullptr ||
          owned->disposition == SourceDisposition::visible_content)
        title->push_back(cell);
      else if (owned->disposition != SourceDisposition::layout_origin &&
               owned->disposition != SourceDisposition::layout_padding)
        return false;
      continue;
    }
    if (owned != nullptr) {
      switch (owned->disposition) {
      case SourceDisposition::visible_content:
        intro_visible = true;
        introduction->push_back(cell);
        break;
      case SourceDisposition::marker_slot:
      case SourceDisposition::layout_origin:
      case SourceDisposition::layout_padding:
        if (intro_visible &&
            owned->disposition == SourceDisposition::marker_slot)
          return false;
        break;
      default:
        return false;
      }
    } else if (intro_visible) {
      introduction->push_back(cell);
    }
  }
  trim_cells(*title);
  trim_cells(*introduction);
  return !title->empty() && !introduction->empty();
}

bool split_title_and_introduction(
    const std::vector<MenuSourceCellIR> &payload_cells,
    const LayoutIR &layout, const OwnershipIR &ownership,
    std::vector<MenuSourceCellIR> *title,
    std::vector<MenuSourceCellIR> *introduction) {
  struct PaddingRun {
    std::size_t begin = 0;
    std::size_t end = 0;
    DisplayRunId run = 0;
    std::size_t row = 0;
  };
  std::vector<PaddingRun> padding_runs;
  for (const auto &cell : payload_cells) {
    const auto *owned = owned_cell(ownership, cell);
    if (owned == nullptr ||
        owned->disposition != SourceDisposition::layout_padding)
      continue;
    if (padding_runs.empty() ||
        padding_runs.back().end != cell.output_word_index ||
        padding_runs.back().run != owned->run ||
        padding_runs.back().row != owned->row_index) {
      padding_runs.push_back({cell.output_word_index,
                              cell.output_word_index + 1, owned->run,
                              owned->row_index});
    } else {
      padding_runs.back().end = cell.output_word_index + 1;
    }
  }

  std::vector<PaddingRun> candidates;
  for (const auto &padding : padding_runs) {
    // The observed menu title/intro form is a fixed 50-column layout: a
    // source-proven gap of at least ten cells ends immediately before column
    // 50. Shorter padding remains ordinary within-field layout.
    if (padding.end - padding.begin < 10)
      continue;
    const MenuSourceCellIR *origin = nullptr;
    const MenuSourceCellIR *before = nullptr;
    const MenuSourceCellIR *after = nullptr;
    for (const auto &cell : payload_cells) {
      const auto *owned = owned_cell(ownership, cell);
      if (owned == nullptr || owned->run != padding.run ||
          owned->row_index != padding.row)
        continue;
      if (owned->disposition == SourceDisposition::layout_origin)
        origin = &cell;
      if (owned->disposition != SourceDisposition::visible_content)
        continue;
      if (cell.output_word_index < padding.begin)
        before = &cell;
      else if (cell.output_word_index >= padding.end && after == nullptr)
        after = &cell;
    }
    const auto run = std::find_if(layout.runs.begin(), layout.runs.end(),
                                  [&](const auto &candidate) {
                                    return candidate.id == padding.run;
                                  });
    if (origin == nullptr || before == nullptr || after == nullptr ||
        run == layout.runs.end() || padding.row >= run->rows.size())
      continue;
    const auto &row = run->rows[padding.row];
    const auto intro_column = row.native_origin +
                              (after->output_word_index -
                               origin->output_word_index) -
                              1;
    if (intro_column == 50)
      candidates.push_back(padding);
  }
  if (candidates.size() > 1)
    return false;
  if (candidates.empty()) {
    if (split_title_and_introduction_at_marker_row(payload_cells, layout,
                                                   ownership, title,
                                                   introduction))
      return true;
    title->clear();
    introduction->clear();
    *title = payload_cells;
    trim_cells(*title);
    return !title->empty();
  }
  const auto &separator = candidates.front();
  for (const auto &cell : payload_cells) {
    if (cell.output_word_index < separator.begin)
      title->push_back(cell);
    else if (cell.output_word_index >= separator.end)
      introduction->push_back(cell);
  }
  trim_cells(*title);
  trim_cells(*introduction);
  return !title->empty() && !introduction->empty();
}

// Splits a raw item's label cells at one source-proven terminal token: the
// cells before it (trailing space trimmed) and the token's own cells.
// Returns false when the split does not leave label text on both sides.
bool split_label_at_token(const MenuItemIR &item, std::size_t token_index,
                          std::size_t label_cell_begin,
                          std::vector<MenuSourceCellIR> *label,
                          std::vector<MenuSourceCellIR> *marker) {
  if (label_cell_begin >= item.label_cells.size()) return false;
  label->assign(item.label_cells.begin(),
                item.label_cells.begin() +
                    static_cast<std::ptrdiff_t>(label_cell_begin));
  marker->clear();
  for (auto cell = item.label_cells.begin() +
                   static_cast<std::ptrdiff_t>(label_cell_begin);
       cell != item.label_cells.end(); ++cell) {
    if (cell->token_index == token_index)
      marker->push_back(*cell);
    else if (!ascii_space(cell->word))
      return false;
  }
  trim_cells(*label);
  return !label->empty() && !marker->empty();
}

// Splits a raw item's label cells at its source-proven compact terminal
// token.
bool split_label_at_compact_terminal(const MenuItemIR &item,
                                     std::vector<MenuSourceCellIR> *label,
                                     std::vector<MenuSourceCellIR> *marker) {
  if (!item.compact_terminal) return false;
  return split_label_at_token(item, item.compact_terminal->token_index,
                              item.compact_terminal->label_cell_begin, label,
                              marker);
}

// Splits a raw item's label cells at its source-proven record terminator
// token (the `.` glyph standing immediately before CEMENU).
bool split_label_at_terminator(const MenuItemIR &item,
                               std::vector<MenuSourceCellIR> *label,
                               std::vector<MenuSourceCellIR> *marker) {
  if (!item.terminator) return false;
  return split_label_at_token(item, item.terminator->token_index,
                              item.terminator->label_cell_begin, label,
                              marker);
}

// Re-splits a raw item at the terminal token a validation entry recorded.
// The token must still be one of the item's two source-proven candidates.
bool split_label_at_validated_terminal(const MenuItemIR &item,
                                       std::size_t token_index,
                                       std::vector<MenuSourceCellIR> *label,
                                       std::vector<MenuSourceCellIR> *marker) {
  if (item.terminator && item.terminator->token_index == token_index)
    return split_label_at_terminator(item, label, marker);
  if (item.compact_terminal &&
      item.compact_terminal->token_index == token_index)
    return split_label_at_compact_terminal(item, label, marker);
  return false;
}

// The one terminal token, if any, whose exclusion makes a raw label agree
// with `canonical`.  The record terminator is tried first: it is proven by
// token adjacency to CEMENU rather than by its display width, so it is the
// stronger evidence when an item carries both.
bool label_without_terminal_token(const MenuItemIR &item,
                                  const std::string &canonical,
                                  std::string *label,
                                  std::size_t *token_index) {
  std::vector<MenuSourceCellIR> label_cells;
  std::vector<MenuSourceCellIR> marker_cells;
  if (split_label_at_terminator(item, &label_cells, &marker_cells)) {
    const auto text = cell_text(label_cells);
    if (ascii_equals_case_insensitive(text, canonical)) {
      *label = text;
      *token_index = item.terminator->token_index;
      return true;
    }
  }
  if (split_label_at_compact_terminal(item, &label_cells, &marker_cells)) {
    const auto text = cell_text(label_cells);
    if (ascii_equals_case_insensitive(text, canonical)) {
      *label = text;
      *token_index = item.compact_terminal->token_index;
      return true;
    }
  }
  return false;
}

} // namespace

std::optional<MenuTargetValidationIR> validate_source_menu_targets(
    const MenuIR &source_menu, const BookTopicCatalogIR &catalog,
    std::string *error) {
  const auto reject =
      [&](std::string message) -> std::optional<MenuTargetValidationIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (source_menu.items.empty())
    return reject("raw menu contains no targets to validate");
  MenuTargetValidationIR result;
  for (const auto &source : source_menu.items) {
    const auto *entry = find_book_topic_catalog_entry(catalog, source.target);
    if (entry == nullptr)
      return reject("raw menu target does not exist in topic catalog: " +
                    source.target);
    const auto has_header = entry->topic_header.has_value();
    const auto has_toc = !entry->toc_entries.empty();
    if (!has_header && !has_toc)
      return reject("raw menu target has no catalog evidence: " +
                    source.target);
    // A raw CMITEM is promoted only when its label independently agrees with
    // the topic header.  A TOC projection is useful corroborating evidence,
    // but choosing it over an available header would admit catalog-specific
    // relabeling and recreate the compatibility repair path this IR replaces.
    const auto &canonical_title = has_header ? entry->topic_header->title
                                             : entry->toc_entries.back().title;
    const auto normalized_canonical =
        collapse_ascii_whitespace(trim_ascii(canonical_title));
    auto label = collapse_ascii_whitespace(trim_ascii(source.text));
    std::optional<std::size_t> terminal_marker_token;
    if (!ascii_equals_case_insensitive(label, normalized_canonical)) {
      // The label may end in one compact display marker (`>`, `[`, `++`) or
      // in the record terminator `.` that stands immediately before CEMENU.
      // Only those two source-proven terminal tokens are candidates: the
      // token's cells are excluded and the remaining label cells must agree
      // with the canonical title instead.  No marker spelling is consulted.
      std::string stripped;
      std::size_t token = 0;
      if (!source.compact_terminal && !source.terminator)
        return reject("raw menu label differs from canonical catalog title: " +
                      source.target);
      if (!label_without_terminal_token(source, normalized_canonical,
                                        &stripped, &token))
        return reject("raw menu label differs from canonical catalog title "
                      "beyond its compact terminal token: " +
                      source.target);
      label = std::move(stripped);
      terminal_marker_token = token;
    }

    MenuTargetValidationEntryIR validated;
    validated.target = source.target;
    validated.label = label;
    validated.terminal_marker_token = terminal_marker_token;
    validated.existence =
        has_header && has_toc
            ? MenuTargetValidationEntryIR::ExistenceEvidence::
                  topic_header_and_toc
            : (has_header
                   ? MenuTargetValidationEntryIR::ExistenceEvidence::
                         topic_header
                   : MenuTargetValidationEntryIR::ExistenceEvidence::toc_entry);
    const auto header_matches =
        has_header &&
        ascii_equals_case_insensitive(
            label, collapse_ascii_whitespace(
                       trim_ascii(entry->topic_header->title)));
    const auto toc_matches =
        has_toc && ascii_equals_case_insensitive(
                       label, collapse_ascii_whitespace(trim_ascii(
                                  entry->toc_entries.back().title)));
    validated.label_evidence =
        header_matches && toc_matches
            ? MenuTargetValidationEntryIR::LabelEvidence::topic_title_and_toc
            : (header_matches
                   ? MenuTargetValidationEntryIR::LabelEvidence::topic_title
                   : MenuTargetValidationEntryIR::LabelEvidence::toc_title);
    result.items.push_back(std::move(validated));
  }
  if (error != nullptr)
    error->clear();
  return result;
}

std::optional<MenuTopicIR>
extract_menu_topic_ir(const std::vector<DecodedLogicalRecordSource> &records,
                      const MenuTargetValidationIR &target_validation,
                      const LayoutIR &layout,
                      const VerifiedOwnershipIR &verified_ownership,
                      std::string *error) {
  const OwnershipIR &ownership = verified_ownership;
  const auto reject = [&](std::string message) -> std::optional<MenuTopicIR> {
    fail(error, std::move(message));
    return std::nullopt;
  };
  if (records.empty())
    return reject("menu topic source is empty");
  std::string inner_error;
  if (!verify_layout_ir(records, layout, &inner_error) ||
      !ownership_verified_for(verified_ownership, records, layout,
                              &inner_error))
    return reject("menu topic prerequisite IR rejected: " + inner_error);
  const auto menu = extract_source_menu_ir(records, &inner_error);
  if (!menu || !verify_source_menu_ir(records, *menu, &inner_error))
    return reject("menu topic source-only menu rejected: " + inner_error);
  if (menu->items.size() != target_validation.items.size())
    return reject("menu topic target validation count differs from source");
  for (std::size_t item = 0; item < menu->items.size(); ++item) {
    const auto &source = menu->items[item];
    const auto &validated = target_validation.items[item];
    if (source.target != validated.target)
      return reject("menu topic target validation differs from source");
    if (!validated.terminal_marker_token) {
      if (source.text != validated.label)
        return reject("menu topic target validation differs from source");
      continue;
    }
    std::vector<MenuSourceCellIR> label_cells;
    std::vector<MenuSourceCellIR> marker_cells;
    if (!split_label_at_validated_terminal(source,
                                           *validated.terminal_marker_token,
                                           &label_cells, &marker_cells) ||
        cell_text(label_cells) != validated.label)
      return reject("menu topic validated terminal marker differs from "
                    "source evidence");
  }

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
  result.title_source = source_slice(*segments[index].record, title_segment);
  const auto payload_cells =
      source_cells(*segments[index].record, title_segment.payload_range);
  std::vector<MenuSourceCellIR> introduction_cells;
  if (!split_title_and_introduction(payload_cells, layout, ownership,
                                    &result.title_cells,
                                    &introduction_cells))
    return reject("menu topic ST title/intro ownership is incomplete");
  result.title = cell_text(result.title_cells);
  if (!introduction_cells.empty())
    result.introductions.push_back(
        {cell_text(introduction_cells), result.title_source,
         std::move(introduction_cells)});
  if (result.title.empty() || result.title_cells.empty())
    return reject("menu topic title text or provenance is empty");
  if (!result.introductions.empty() &&
      result.introductions.front().text.empty())
    return reject("menu topic introduction text is empty");
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
  for (const auto &paragraph : result.introductions)
    if (!insert_unique_cells(visible_cells, paragraph.cells))
      return reject("menu topic introduction source cells overlap");
  std::size_t menu_index = 0;
  while (index < segments.size() &&
         segments[index].segment->kind == BookControlKind::menu_item) {
    if (menu_index >= menu->items.size())
      return reject("menu topic contains an unverified extra CMITEM");
    const auto &segment = *segments[index].segment;
    const auto &item = menu->items[menu_index];
    if (segment.malformed ||
        item.logical_record != segments[index].record->logical_record ||
        item.segment_index != segment.segment_index)
      return reject("menu topic CMITEM does not match verified menu order");
    MenuTopicItemIR semantic;
    semantic.target = {CrossReferenceTargetKindIR::topic, item.target};
    semantic.source = source_slice(*segments[index].record, segment);
    semantic.target_cells = item.target_cells;
    const auto &validated = target_validation.items[menu_index];
    if (validated.terminal_marker_token) {
      if (!split_label_at_validated_terminal(item,
                                             *validated.terminal_marker_token,
                                             &semantic.label_cells,
                                             &semantic.marker_cells))
        return reject("menu topic terminal marker cells are unavailable");
      semantic.label = cell_text(semantic.label_cells);
    } else {
      semantic.label = item.text;
      semantic.label_cells = item.label_cells;
    }
    if (semantic.label != validated.label)
      return reject("menu topic label differs from validated label");
    if (!insert_unique_cells(visible_cells, semantic.target_cells) ||
        !insert_unique_cells(visible_cells, semantic.label_cells) ||
        !insert_unique_cells(visible_cells, semantic.marker_cells))
      return reject("menu topic visible source cell is owned more than once");
    result.items.push_back(std::move(semantic));
    ++menu_index;
    ++index;
  }
  if (menu_index != menu->items.size())
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
    const std::vector<DecodedLogicalRecordSource> &records,
    const MenuTargetValidationIR &target_validation,
    const LayoutIR &layout, const VerifiedOwnershipIR &ownership,
    const MenuTopicIR &topic, std::string *error) {
  const auto canonical =
      extract_menu_topic_ir(records, target_validation, layout, ownership,
                            error);
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
      << " introductions=" << topic.introductions.size()
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
        << " label_cells=" << item.label_cells.size()
        << " marker_cells=" << item.marker_cells.size() << '\n';
  for (const auto &segment : topic.segments)
    out << "segment=" << segment.source.logical_record << ':'
        << segment.source.segment_index << " opcode='" << segment.opcode
        << "'\n";
  return out.str();
}

} // namespace geist::detail
