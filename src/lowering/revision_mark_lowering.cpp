// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/revision_mark_lowering.hpp"

#include "geist/detail/layout/display_lines.hpp"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>
#include <variant>

namespace geist::detail {
namespace {

// What the rows under one inline say about its revision.
struct RowMarks {
  bool drawn = false;      // the inline names at least one drawn token
  bool unrevised = false;  // ... and one of them stands on an unrevised row
  bool undecided = false;  // ... or on a record whose rows are not framed
  std::optional<char32_t> mark;
  bool mixed_marks = false;
};

class RowMarkReader {
public:
  explicit RowMarkReader(const std::vector<DecodedLogicalRecordSource> &sources) {
    for (const auto &record : sources) records_.emplace(record.logical_record, &record);
  }

  // The mark every drawn token of `origin` stands under, `0` when every one
  // stands on an unrevised row, and nothing when the origin names no drawn
  // token or its rows disagree.
  std::optional<char32_t> mark_of(const DocumentNodeOriginIR &origin) {
    RowMarks marks;
    for (const auto &slice : origin.slices) read(slice, marks);
    if (!marks.drawn || marks.undecided || marks.mixed_marks)
      return std::nullopt;
    if (marks.mark && marks.unrevised) return std::nullopt;
    return marks.mark ? *marks.mark : char32_t{0};
  }

private:
  void read(const DocumentSourceSliceIR &slice, RowMarks &marks) {
    const auto found = records_.find(slice.logical_record);
    if (found == records_.end()) {
      marks.undecided = true;
      return;
    }
    const auto &record = *found->second;
    for (auto token = slice.token_begin;
         token < slice.token_end && token < record.ir.tokens.size(); ++token) {
      const auto &words = record.ir.tokens[token].decoded_words;
      if (!std::any_of(words.begin(), words.end(),
                       [](const auto word) { return word > ' '; }))
        continue;
      const auto *line = display_line_of_token(record, token);
      if (line == nullptr) {
        marks.undecided = true;
        continue;
      }
      if (token == line->prefix_token) continue;
      if (!body_row(record, *line)) continue;
      if (line->margin_mark_place == MarginMarkPlaceIR::row_margin &&
          line->margin_mark) {
        if (token == line->margin_mark_token) continue;
        marks.drawn = true;
        if (marks.mark && *marks.mark != *line->margin_mark)
          marks.mixed_marks = true;
        marks.mark = *line->margin_mark;
      } else {
        marks.drawn = true;
        marks.unrevised = true;
      }
    }
  }

  // A row of the body: its cell 0 is blank (the margin).  A control or
  // title line opens at cell 0 and has no margin.
  bool body_row(const DecodedLogicalRecordSource &record,
                const DisplayLineIR &line) {
    const auto key = std::make_pair(record.logical_record, line.prefix_token);
    const auto cached = body_rows_.find(key);
    if (cached != body_rows_.end()) return cached->second;
    const auto cells = display_line_cells(record, line);
    const auto row = !cells.empty() && cells.front().word == ' ';
    body_rows_.emplace(key, row);
    return row;
  }

  std::map<std::uint32_t, const DecodedLogicalRecordSource *> records_;
  std::map<std::pair<std::uint32_t, std::size_t>, bool> body_rows_;
};

DocumentNodeOriginIR run_origin(const InlineSequenceIR &children) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.detail = "revised rows";
  for (const auto &child : children) {
    result.slices.insert(result.slices.end(), child.origin.slices.begin(),
                         child.origin.slices.end());
    result.rows.insert(result.rows.end(), child.origin.rows.begin(),
                       child.origin.rows.end());
  }
  // The run owns its children's tokens whole, in source order and once
  // each, as a container origin does.
  for (auto &slice : result.slices) {
    slice.character_begin = 0;
    slice.character_end = 0;
  }
  const auto key = [](const DocumentSourceSliceIR &slice) {
    return std::make_tuple(slice.logical_record, slice.segment_index,
                           slice.token_begin, slice.token_end,
                           slice.byte_begin, slice.byte_end);
  };
  std::sort(result.slices.begin(), result.slices.end(),
            [&](const auto &left, const auto &right) {
              return key(left) < key(right);
            });
  result.slices.erase(std::unique(result.slices.begin(), result.slices.end(),
                                  [&](const auto &left, const auto &right) {
                                    return key(left) == key(right);
                                  }),
                      result.slices.end());
  const auto row_key = [](const DocumentSourceRowIR &row) {
    return std::make_pair(row.display_run, row.row_index);
  };
  std::sort(result.rows.begin(), result.rows.end(),
            [&](const auto &left, const auto &right) {
              return row_key(left) < row_key(right);
            });
  result.rows.erase(std::unique(result.rows.begin(), result.rows.end(),
                                [&](const auto &left, const auto &right) {
                                  return row_key(left) == row_key(right);
                                }),
                    result.rows.end());
  return result;
}

void carry_sequence(InlineSequenceIR &content, RowMarkReader &reader) {
  if (content.empty() ||
      std::any_of(content.begin(), content.end(), [](const InlineIR &in) {
        return std::holds_alternative<RevisedInlineIR>(in.node);
      }))
    return;
  // 0 = unrevised, otherwise the mark; an inline that decides nothing takes
  // the mark of the runs on both sides of it when they agree.
  std::vector<std::optional<char32_t>> decided;
  decided.reserve(content.size());
  for (const auto &in : content) decided.push_back(reader.mark_of(in.origin));
  std::vector<char32_t> marks(content.size(), 0);
  for (std::size_t index = 0; index < content.size(); ++index) {
    if (decided[index]) {
      marks[index] = *decided[index];
      continue;
    }
    std::optional<char32_t> before;
    for (auto at = index; at-- > 0;)
      if (decided[at]) {
        before = decided[at];
        break;
      }
    std::optional<char32_t> after;
    for (auto at = index + 1; at < content.size(); ++at)
      if (decided[at]) {
        after = decided[at];
        break;
      }
    if (before && after && *before == *after) marks[index] = *before;
  }
  if (std::all_of(marks.begin(), marks.end(),
                  [](const char32_t mark) { return mark == 0; }))
    return;
  InlineSequenceIR rebuilt;
  for (std::size_t index = 0; index < content.size();) {
    if (marks[index] == 0) {
      rebuilt.push_back(std::move(content[index]));
      ++index;
      continue;
    }
    RevisedInlineIR revised;
    revised.mark = marks[index];
    auto at = index;
    for (; at < content.size() && marks[at] == marks[index]; ++at)
      revised.children.push_back(std::move(content[at]));
    InlineIR wrapper;
    wrapper.origin = run_origin(revised.children);
    wrapper.node = std::move(revised);
    rebuilt.push_back(std::move(wrapper));
    index = at;
  }
  content = std::move(rebuilt);
}

void carry_blocks(BlockSequenceIR &blocks, RowMarkReader &reader);

void carry_item(ListItemIR &item, RowMarkReader &reader) {
  if (!item.content.empty() && !item.revision_mark) {
    carry_sequence(item.content, reader);
    if (item.label.empty() && item.content.size() == 1)
      if (auto *revised = std::get_if<RevisedInlineIR>(&item.content.front().node)) {
        item.revision_mark = revised->mark;
        auto children = std::move(revised->children);
        item.content = std::move(children);
      }
  }
  carry_blocks(item.blocks, reader);
}

void carry_blocks(BlockSequenceIR &blocks, RowMarkReader &reader) {
  for (auto &block : blocks)
    std::visit(
        [&](auto &node) {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, ParagraphBlockIR>) {
            carry_sequence(node.content, reader);
          } else if constexpr (std::is_same_v<T, ListBlockIR>) {
            for (auto &item : node.items) carry_item(item, reader);
          } else if constexpr (std::is_same_v<T, DefinitionListBlockIR>) {
            for (auto &entry : node.entries) {
              carry_sequence(entry.term, reader);
              carry_sequence(entry.definition, reader);
              carry_blocks(entry.definition_blocks, reader);
            }
          } else if constexpr (std::is_same_v<T, NoteBlockIR>) {
            carry_sequence(node.content, reader);
          } else if constexpr (std::is_same_v<T, PublicationListBlockIR>) {
            for (auto &entry : node.entries) {
              carry_sequence(entry.title, reader);
              for (auto &paragraph : entry.paragraphs)
                carry_sequence(paragraph, reader);
            }
          } else if constexpr (std::is_same_v<T, FootnoteBlockIR>) {
            carry_sequence(node.content, reader);
          } else if constexpr (std::is_same_v<T, FigureBlockIR>) {
            // A figure's caption is a row of the body like any other:
            // CNMYB0MS 1.1.4 ` | Figure 1. ...` (hosted prints the mark).
            carry_sequence(node.caption, reader);
          }
        },
        block.node);
}

}  // namespace

void carry_row_revision_marks(
    DocumentIR &document,
    const std::vector<DecodedLogicalRecordSource> &sources) {
  RowMarkReader reader(sources);
  carry_blocks(document.blocks, reader);
}

void carry_caption_revision_marks(
    DocumentIR &document,
    const std::vector<DecodedLogicalRecordSource> &sources) {
  RowMarkReader reader(sources);
  for (auto &block : document.blocks) {
    if (auto *figure = std::get_if<FigureBlockIR>(&block.node)) {
      carry_sequence(figure->caption, reader);
      continue;
    }
    auto *paragraph = std::get_if<ParagraphBlockIR>(&block.node);
    if (paragraph == nullptr) continue;
    const auto &detail = block.origin.detail;
    if (detail == "figure caption" || detail == "ordered figure caption" ||
        detail == "fixed table caption")
      carry_sequence(paragraph->content, reader);
  }
}

}  // namespace geist::detail
