#include "geist/detail/message_section_blocks_ir.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <utility>

namespace geist::detail {
namespace {

using CellKey = std::tuple<std::uint32_t, std::size_t, std::size_t>;

struct RowView {
  MessageSourceRowIR source;
  const PhysicalRowIR *physical = nullptr;
  const MessageSemanticRowIR *semantic = nullptr;
  std::vector<const PositionedRowCellIR *> cells;
};

MessageStructuredSourceCellIR source_cell(const PositionedRowCellIR &cell) {
  return {cell.logical_record, cell.token_index, cell.word_index,
          cell.word,           cell.role,        cell.display_column};
}

CellKey key(const MessageStructuredSourceCellIR &cell) {
  return {cell.logical_record, cell.token_index, cell.word_index};
}

CellKey key(const PositionedRowCellIR &cell) {
  return {cell.logical_record, cell.token_index, cell.word_index};
}

std::string compact(std::string value) {
  return collapse_ascii_whitespace(trim_ascii(std::move(value)));
}

void append_text(std::string &target, const std::string &value) {
  const auto text = compact(value);
  if (text.empty())
    return;
  if (!target.empty())
    target.push_back(' ');
  target += text;
}

struct WideSplit {
  std::size_t second_text = 0;
  std::string left;
  std::string right;
};

std::optional<WideSplit> wide_split(const std::string &text,
                                    std::size_t minimum = 2) {
  for (std::size_t begin = 0; begin < text.size();) {
    if (text[begin] != ' ') {
      ++begin;
      continue;
    }
    auto end = begin;
    while (end < text.size() && text[end] == ' ')
      ++end;
    if (end - begin >= minimum) {
      auto left = compact(text.substr(0, begin));
      auto right = compact(text.substr(end));
      if (!left.empty() && !right.empty())
        return WideSplit{end, std::move(left), std::move(right)};
    }
    begin = end;
  }
  return std::nullopt;
}

std::optional<std::size_t> first_content_column(const RowView &row) {
  std::optional<std::size_t> result;
  for (const auto *cell : row.cells) {
    if (cell->role != RowCellRole::content || !cell->display_column)
      continue;
    result = result ? std::min(*result, *cell->display_column)
                    : cell->display_column;
  }
  return result;
}

std::vector<RowView> section_rows(const LayoutIR &layout,
                                  const OwnershipIR &ownership,
                                  const MessageSectionIR &section) {
  std::map<MessageSourceRowIR, const PhysicalRowIR *> physical;
  for (const auto &run : layout.runs)
    for (std::size_t row = 0; row < run.rows.size(); ++row)
      physical[{run.id, row}] = &run.rows[row];
  std::map<MessageSourceRowIR, std::vector<const PositionedRowCellIR *>> cells;
  for (const auto &cell : ownership.row_cells)
    cells[{cell.run, cell.row_index}].push_back(&cell);

  std::vector<RowView> result;
  std::set<MessageSourceRowIR> seen;
  for (const auto &paragraph : section.paragraphs) {
    for (const auto &semantic : paragraph.semantic_rows) {
      if (!seen.insert(semantic.source_row).second)
        continue;
      const auto found = physical.find(semantic.source_row);
      const auto owned = cells.find(semantic.source_row);
      if (found == physical.end() || owned == cells.end())
        continue;
      auto row_cells = owned->second;
      std::stable_sort(row_cells.begin(), row_cells.end(),
                       [](const auto *left, const auto *right) {
                         if (left->display_column != right->display_column)
                           return left->display_column < right->display_column;
                         return key(*left) < key(*right);
                       });
      result.push_back({semantic.source_row, found->second, &semantic,
                        std::move(row_cells)});
    }
  }
  return result;
}

void assign_row_cells(const RowView &row, const std::size_t split_column,
                      MessageStructuredTableRowIR &target,
                      const bool boundary_is_right) {
  target.cells.resize(2);
  target.cells[0].column = first_content_column(row).value_or(0);
  target.cells[1].column = split_column;
  target.cells[0].source_rows.push_back(row.source);
  target.cells[1].source_rows.push_back(row.source);
  for (const auto *owned : row.cells) {
    const auto cell = source_cell(*owned);
    if (owned->role == RowCellRole::content && owned->display_column) {
      target.cells[*owned->display_column < split_column ? 0 : 1]
          .source_cells.push_back(cell);
    } else if (owned->role == RowCellRole::boundary && boundary_is_right) {
      target.cells[1].source_cells.push_back(cell);
    } else {
      target.structural_cells.push_back(cell);
    }
  }
}

void append_continuation(const RowView &row,
                         MessageStructuredTableRowIR &target) {
  auto &right = target.cells[1];
  append_text(right.text, row.semantic->text);
  right.source_rows.push_back(row.source);
  const auto lexical_boundary =
      row.semantic->marker_disposition ==
          MessageMarkerDispositionIR::lexical_prefix ||
      row.semantic->marker_disposition ==
          MessageMarkerDispositionIR::list_prefix;
  for (const auto *owned : row.cells) {
    const auto cell = source_cell(*owned);
    if (owned->role == RowCellRole::content ||
        (owned->role == RowCellRole::boundary && lexical_boundary))
      right.source_cells.push_back(cell);
    else
      target.structural_cells.push_back(cell);
  }
}

// The row's semantic projection without a compact marker this block claims as
// structural evidence. Message semantics may have carried a lexical marker
// spelling into the row text; once the row is admitted as a fixed-field table
// row, that marker is positioned boundary evidence, so exactly the marker's
// decoded text is removed from the front of the projection. Everything else in
// the semantic text (restored delimiters, attached opaque fields) is kept so
// the block conserves the same words as the flattened section.
std::optional<std::string> structural_row_text(const RowView &row) {
  auto text = compact(row.semantic->text);
  const auto lexical_marker =
      row.physical->marker &&
      (row.semantic->marker_disposition ==
           MessageMarkerDispositionIR::lexical_prefix ||
       row.semantic->marker_disposition ==
           MessageMarkerDispositionIR::list_prefix);
  if (!lexical_marker)
    return text;
  const auto marker = compact(row.physical->marker->decoded_text);
  if (text == marker)
    return std::string{};
  if (text.size() > marker.size() && text.compare(0, marker.size(), marker) == 0 &&
      text[marker.size()] == ' ')
    return text.substr(marker.size() + 1);
  return std::nullopt;
}

// Splits the row's structural text at the physically positioned first column
// key. The key must open the semantic text exactly; the remainder is the
// second column's text.
std::optional<std::pair<std::string, std::string>>
split_semantic_text(const RowView &row, const std::string &key) {
  const auto text = structural_row_text(row);
  if (!text)
    return std::nullopt;
  if (*text == key)
    return std::make_pair(key, std::string{});
  if (text->size() > key.size() && text->compare(0, key.size(), key) == 0 &&
      (*text)[key.size()] == ' ')
    return std::make_pair(key, text->substr(key.size() + 1));
  return std::nullopt;
}

bool numeric_key(const std::string &value) {
  return value.size() >= 5 && value.size() <= 6 &&
         std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
           return std::isdigit(ch) != 0;
         });
}

std::optional<MessageStructuredTableBlockIR>
command_table(const std::vector<RowView> &rows) {
  auto header = rows.end();
  WideSplit header_split;
  for (auto row = rows.begin(); row != rows.end(); ++row) {
    const auto split = wide_split(row->physical->visible_text, 2);
    if (split && ascii_equals_case_insensitive(split->left, "Command type") &&
        ascii_equals_case_insensitive(split->right, "Command")) {
      header = row;
      header_split = *split;
      break;
    }
  }
  if (header == rows.end())
    return std::nullopt;
  const auto header_first = first_content_column(*header);
  if (!header_first)
    return std::nullopt;
  const auto header_second = *header_first + header_split.second_text;

  const auto header_text = split_semantic_text(*header, header_split.left);
  if (!header_text || header_text->second != header_split.right)
    return std::nullopt;

  MessageStructuredTableBlockIR table;
  assign_row_cells(*header, header_second, table.header, false);
  table.header.cells[0].column = 0;
  table.header.cells[1].column = header_split.second_text;
  table.header.cells[0].text = header_text->first;
  table.header.cells[1].text = header_text->second;

  std::set<std::string> keys;
  for (auto row = std::next(header); row != rows.end(); ++row) {
    const auto split = wide_split(row->physical->visible_text, 2);
    const auto first = first_content_column(*row);
    const auto primary =
        split && first && numeric_key(split->left) && split->second_text == 15;
    if (primary) {
      const auto second = *first + split->second_text;
      if (!keys.insert(split->left).second)
        return std::nullopt;
      const auto text = split_semantic_text(*row, split->left);
      if (!text || text->second.empty())
        return std::nullopt;
      MessageStructuredTableRowIR item;
      assign_row_cells(*row, second, item, false);
      item.cells[0].column = 0;
      item.cells[1].column = split->second_text;
      item.cells[0].text = text->first;
      item.cells[1].text = text->second;
      table.rows.push_back(std::move(item));
    } else {
      if (table.rows.empty())
        return std::nullopt;
      append_continuation(*row, table.rows.back());
    }
  }
  if (table.rows.size() != 25)
    return std::nullopt;
  return table;
}

std::vector<const PositionedRowCellIR *>
content_cells_in_range(const RowView &row, const std::size_t begin,
                       const std::optional<std::size_t> end) {
  std::vector<const PositionedRowCellIR *> result;
  for (const auto *cell : row.cells)
    if (cell->role == RowCellRole::content && cell->display_column &&
        *cell->display_column >= begin &&
        (!end || *cell->display_column < *end))
      result.push_back(cell);
  return result;
}

std::optional<MessageStructuredListBlockIR>
hanging_list(const std::vector<RowView> &rows) {
  constexpr auto lead_text = "Verify that the following conditions are true:";
  auto lead = rows.end();
  std::size_t lead_at = 0;
  for (auto row = rows.begin(); row != rows.end(); ++row) {
    lead_at = row->physical->visible_text.find(lead_text);
    if (lead_at != std::string::npos) {
      lead = row;
      break;
    }
  }
  if (lead == rows.end())
    return std::nullopt;
  const auto lead_first = first_content_column(*lead);
  if (!lead_first)
    return std::nullopt;

  MessageStructuredListBlockIR list;
  list.lead_in.text = lead_text;
  list.lead_in.column = *lead_first + lead_at;
  list.lead_in.source_rows.push_back(lead->source);
  for (const auto *cell :
       content_cells_in_range(*lead, list.lead_in.column, std::nullopt))
    list.lead_in.source_cells.push_back(source_cell(*cell));

  bool saw_return_to_prose = false;
  for (auto row = std::next(lead); row != rows.end(); ++row) {
    const auto origin = row->physical->native_origin;
    if (origin != 13 && origin != 17)
      break;
    if (row->semantic->marker_disposition !=
        MessageMarkerDispositionIR::layout_artifact)
      return std::nullopt;
    auto text = row->semantic->text;
    std::optional<std::size_t> cell_end;
    const auto prose =
        row->physical->visible_text.find("If everything is correctly set");
    if (prose != std::string::npos) {
      const auto first = first_content_column(*row);
      if (!first)
        return std::nullopt;
      cell_end = *first + prose;
      const auto semantic_prose = text.find("If everything is correctly set");
      if (semantic_prose == std::string::npos)
        return std::nullopt;
      text = text.substr(0, semantic_prose);
      saw_return_to_prose = true;
    }
    if (origin == 13) {
      list.items.push_back({});
    } else if (list.items.empty()) {
      return std::nullopt;
    }
    auto &item = list.items.back();
    append_text(item.text, text);
    item.source_rows.push_back(row->source);
    const auto begin = first_content_column(*row);
    if (!begin)
      return std::nullopt;
    const auto selected = content_cells_in_range(*row, *begin, cell_end);
    std::set<CellKey> semantic_cells;
    for (const auto *cell : selected) {
      item.source_cells.push_back(source_cell(*cell));
      semantic_cells.insert(key(*cell));
    }
    for (const auto *cell : row->cells) {
      if (semantic_cells.count(key(*cell)) != 0)
        continue;
      if (cell_end && cell->display_column &&
          *cell->display_column >= *cell_end)
        continue;
      item.structural_cells.push_back(source_cell(*cell));
    }
    if (saw_return_to_prose)
      break;
  }
  if (list.items.size() != 3 || !saw_return_to_prose)
    return std::nullopt;
  return list;
}

std::optional<MessageStructuredPreformattedBlockIR>
application_table_fallback(const std::vector<RowView> &rows,
                           const MessageSectionIR &section) {
  auto header = rows.end();
  for (auto row = rows.begin(); row != rows.end(); ++row) {
    const auto split = wide_split(row->physical->visible_text, 2);
    if (split && ascii_equals_case_insensitive(split->left, "Application") &&
        ascii_equals_case_insensitive(split->right, "Action")) {
      header = row;
      break;
    }
  }
  if (header == rows.end())
    return std::nullopt;
  std::set<std::pair<std::uint32_t, std::size_t>> positioned_tokens;
  for (auto row = header; row != rows.end(); ++row)
    for (const auto *cell : row->cells)
      positioned_tokens.insert({cell->logical_record, cell->token_index});
  const auto slice_has_unpositioned_token = [&](const auto &slice) {
    for (auto token = slice.token_begin; token < slice.token_end; ++token)
      if (positioned_tokens.count({slice.logical_record, token}) == 0)
        return true;
    return false;
  };
  // A recovered paragraph that owns text but no positioned row is source the
  // table cannot place (MSG508's record-leading `SNMP Trap` primary row).
  const auto has_rowless_paragraph =
      std::any_of(section.paragraphs.begin(), section.paragraphs.end(),
                  [](const auto &paragraph) {
                    return !paragraph.text.empty() &&
                           paragraph.semantic_rows.empty();
                  });
  const auto has_unpositioned_slice =
      has_rowless_paragraph ||
      std::any_of(header, rows.end(), [&](const auto &row) {
        return std::any_of(row.semantic->leading_source_slices.begin(),
                           row.semantic->leading_source_slices.end(),
                           slice_has_unpositioned_token) ||
               std::any_of(row.semantic->trailing_source_slices.begin(),
                           row.semantic->trailing_source_slices.end(),
                           slice_has_unpositioned_token);
      });
  if (!has_unpositioned_slice)
    return std::nullopt;

  MessageStructuredPreformattedBlockIR fallback;
  fallback.provenance_complete = false;
  fallback.fallback_reason =
      "table candidate has an unpositioned source continuation";
  for (auto row = header; row != rows.end(); ++row) {
    MessageStructuredPreformattedLineIR line;
    line.text = row->semantic->text;
    line.source_row = row->source;
    for (const auto *cell : row->cells)
      line.source_cells.push_back(source_cell(*cell));
    if (!line.text.empty() && !line.source_cells.empty())
      fallback.lines.push_back(std::move(line));
  }
  return fallback.lines.empty() ? std::nullopt
                                : std::optional{std::move(fallback)};
}

void emit_cell(std::ostringstream &out,
               const MessageStructuredSourceCellIR &cell) {
  out << cell.logical_record << ':' << cell.token_index << ':'
      << cell.word_index << '=' << cell.word << ':'
      << static_cast<int>(cell.role) << '@';
  if (cell.display_column)
    out << *cell.display_column;
  else
    out << '-';
}

template <typename Collection>
void emit_cells(std::ostringstream &out, const Collection &cells) {
  for (const auto &cell : cells) {
    emit_cell(out, cell);
    out << ',';
  }
}

template <typename Collection>
void emit_rows(std::ostringstream &out, const Collection &rows) {
  for (const auto &row : rows)
    out << row.first << ':' << row.second << ',';
}

template <typename Visitor>
void visit_claims(const MessageSectionBlockIR &block, Visitor visitor) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, MessageStructuredTableBlockIR>) {
          const auto visit_row = [&](const auto &row) {
            for (const auto &cell : row.cells)
              for (const auto &source : cell.source_cells)
                visitor(source);
            for (const auto &source : row.structural_cells)
              visitor(source);
          };
          visit_row(node.header);
          for (const auto &row : node.rows)
            visit_row(row);
        } else if constexpr (std::is_same_v<T, MessageStructuredListBlockIR>) {
          for (const auto &source : node.lead_in.source_cells)
            visitor(source);
          for (const auto &item : node.items) {
            for (const auto &source : item.source_cells)
              visitor(source);
            for (const auto &source : item.structural_cells)
              visitor(source);
          }
        } else {
          for (const auto &line : node.lines)
            for (const auto &source : line.source_cells)
              visitor(source);
        }
      },
      block.node);
}

} // namespace

MessageSectionBlocksIR
extract_message_section_blocks_ir(const LayoutIR &layout,
                                  const OwnershipIR &ownership,
                                  const MessageCatalogIR &catalog) {
  MessageSectionBlocksIR result;
  if (!ownership.conflicts.empty())
    return result;
  for (std::size_t entry_index = 0; entry_index < catalog.entries.size();
       ++entry_index) {
    const auto &entry = catalog.entries[entry_index];
    for (std::size_t section_index = 0; section_index < entry.sections.size();
         ++section_index) {
      const auto &section = entry.sections[section_index];
      const auto rows = section_rows(layout, ownership, section);
      if (rows.empty())
        continue;
      if (auto table = command_table(rows)) {
        result.blocks.push_back(
            {entry_index, section_index, std::move(*table)});
      } else if (auto list = hanging_list(rows)) {
        result.blocks.push_back({entry_index, section_index, std::move(*list)});
      } else if (auto fallback = application_table_fallback(rows, section)) {
        result.blocks.push_back(
            {entry_index, section_index, std::move(*fallback)});
      }
    }
  }
  return result;
}

bool verify_message_section_blocks_ir(const LayoutIR &layout,
                                      const OwnershipIR &ownership,
                                      const MessageCatalogIR &catalog,
                                      const MessageSectionBlocksIR &blocks,
                                      std::string *error) {
  const auto fail = [&](const std::string &message) {
    if (error)
      *error = message;
    return false;
  };
  if (!ownership.conflicts.empty())
    return fail("structured message ownership is conflicted");
  const auto canonical =
      extract_message_section_blocks_ir(layout, ownership, catalog);
  if (format_message_section_blocks_ir(canonical) !=
      format_message_section_blocks_ir(blocks))
    return fail("structured message blocks differ from canonical extraction");
  std::map<CellKey, const PositionedRowCellIR *> owned;
  for (const auto &cell : ownership.row_cells)
    owned[key(cell)] = &cell;
  std::set<CellKey> globally_claimed;
  for (const auto &block : blocks.blocks) {
    if (block.entry_index >= catalog.entries.size() ||
        block.section_index >=
            catalog.entries[block.entry_index].sections.size())
      return fail("structured message block owner is invalid");
    std::set<CellKey> claimed;
    bool valid = true;
    visit_claims(block, [&](const auto &cell) {
      const auto found = owned.find(key(cell));
      if (!claimed.insert(key(cell)).second || found == owned.end() ||
          !globally_claimed.insert(key(cell)).second ||
          found->second->word != cell.word ||
          found->second->role != cell.role ||
          found->second->display_column != cell.display_column)
        valid = false;
    });
    if (!valid)
      return fail("structured message source cell is absent or duplicated");
    if (claimed.empty())
      return fail("structured message block claims no source cells");
  }
  if (error)
    error->clear();
  return true;
}

std::string
format_message_section_blocks_ir(const MessageSectionBlocksIR &blocks) {
  std::ostringstream out;
  for (const auto &block : blocks.blocks) {
    out << "block owner=" << block.entry_index << ':' << block.section_index;
    std::visit(
        [&](const auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, MessageStructuredTableBlockIR>) {
            out << " kind=table rows=" << node.rows.size() << '\n';
            const auto emit_row = [&](const char *kind, const auto &row) {
              out << kind;
              for (const auto &cell : row.cells) {
                out << " cell='" << cell.text << "' column=" << cell.column
                    << " rows=";
                emit_rows(out, cell.source_rows);
                out << " sources=";
                emit_cells(out, cell.source_cells);
              }
              out << " structural=";
              emit_cells(out, row.structural_cells);
              out << '\n';
            };
            emit_row("header", node.header);
            for (const auto &row : node.rows)
              emit_row("row", row);
          } else if constexpr (std::is_same_v<T,
                                              MessageStructuredListBlockIR>) {
            out << " kind=list items=" << node.items.size() << " lead='"
                << node.lead_in.text << "' lead_rows=";
            emit_rows(out, node.lead_in.source_rows);
            out << " lead_sources=";
            emit_cells(out, node.lead_in.source_cells);
            out << '\n';
            for (const auto &item : node.items) {
              out << "item='" << item.text << "' rows=";
              emit_rows(out, item.source_rows);
              out << " sources=";
              emit_cells(out, item.source_cells);
              out << " structural=";
              emit_cells(out, item.structural_cells);
              out << '\n';
            }
          } else {
            out << " kind=preformatted complete="
                << (node.provenance_complete ? "yes" : "no") << " reason='"
                << node.fallback_reason << "'\n";
            for (const auto &line : node.lines) {
              out << "line='" << line.text
                  << "' source=" << line.source_row.first << ':'
                  << line.source_row.second << " cells=";
              emit_cells(out, line.source_cells);
              out << '\n';
            }
          }
        },
        block.node);
  }
  return out.str();
}

} // namespace geist::detail

