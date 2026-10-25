// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "geist/detail/lowering/cross_reference_target.hpp"

#include "geist/detail/ir/figure_block_ir.hpp"
#include "geist/detail/ir/selector_link_ir.hpp"
#include "geist/detail/lowering/figure_document_lowering.hpp"
#include "geist/detail/lowering/fixed_table_document_lowering.hpp"
#include "geist/detail/lowering/font_style_inlines.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <tuple>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

DocumentNodeOriginIR origin(std::vector<DocumentSourceSliceIR> slices,
                            std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.slices = std::move(slices);
  result.detail = std::move(detail);
  return result;
}

EmphasisKindIR emphasis_kind(FontStyleIR style) {
  return font_style_emphasis_kind(style);
}

PhraseKindIR phrase_kind(FontStyleIR style) {
  return font_style_phrase_kind(style);
}

CrossReferenceTargetIR prose_target(
    CrossReferenceTargetKindIR kind, const std::string& value,
    const std::vector<std::string>& alternatives) {
  return selector_cross_reference_target(kind, value, alternatives);
}

bool code_style(FontStyleIR style) { return font_style_is_code(style); }

// A table span inside a picture-less frame that draws rows of its own
// (`ProseSpanIR::frame_before`/`frame_after`).  Hosted BookServer serves the
// frame and the table as one `<pre>` (ITPPIBOK 4.3.5.4, DT 19910628074854):
// the frame's top rule and rails, the table's rows with the table anchor on
// the first of them, then the frame's rails and bottom rule.  The table must
// therefore have lowered to its anchor and one verbatim block; the frame's
// rows join that block around the table's rows, and the anchor moves onto the
// table's first row.  A table that lowered to anything else (a declared
// `:table`, a picture, a spot anchor) has no single region to join, and the
// span declines rather than print the frame apart from it.
bool frame_table_rows(const ProseSpanIR& span, std::vector<BlockIR>& blocks,
                      std::string* error) {
  if (blocks.size() != 2 ||
      !std::holds_alternative<AnchorBlockIR>(blocks[0].node) ||
      !std::holds_alternative<PreformattedBlockIR>(blocks[1].node))
    return fail(error, "framed table does not lower to one verbatim region");
  auto& table = std::get<PreformattedBlockIR>(blocks[1].node);
  if (!table.line_links.empty() || !table.line_anchors.empty() ||
      table.line_origins.size() != table.lines.size() ||
      (!table.styled_lines.empty() &&
       table.styled_lines.size() != table.lines.size()))
    return fail(error, "framed table region carries inline structure");
  // The table's rows may carry the styles of their `CFONT` lines (ITPPIBOK
  // 4.3.5.4: `TASK`, `START`, `DATE`); the frame's own rows carry none and
  // join the styled region as plain text.
  const auto styled = !table.styled_lines.empty();
  PreformattedBlockIR framed;
  const auto add = [&](const std::vector<ProseFrameRowIR>& rows) {
    for (const auto& row : rows) {
      framed.lines.push_back(row.text);
      auto row_origin = origin({}, row.text.empty()
                                       ? "prose figure frame: blank row"
                                       : "prose figure frame: drawn row");
      if (row.source.byte_begin < row.source.byte_end)
        row_origin.slices.push_back(row.source);
      if (styled) {
        InlineSequenceIR plain;
        if (!row.text.empty())
          plain.push_back({TextInlineIR{row.text}, row_origin});
        framed.styled_lines.push_back(std::move(plain));
      }
      framed.line_origins.push_back(std::move(row_origin));
    }
  };
  add(span.frame_before);
  framed.line_anchors.push_back(
      {framed.lines.size(), std::get<AnchorBlockIR>(blocks[0].node),
       blocks[0].origin});
  for (std::size_t line = 0; line < table.lines.size(); ++line) {
    framed.lines.push_back(table.lines[line]);
    framed.line_origins.push_back(table.line_origins[line]);
    if (styled) framed.styled_lines.push_back(table.styled_lines[line]);
  }
  add(span.frame_after);
  auto framed_origin = blocks[1].origin;
  framed_origin.detail = "fixed table region in its figure frame";
  BlockIR block;
  block.node = std::move(framed);
  block.origin = std::move(framed_origin);
  blocks.clear();
  blocks.push_back(std::move(block));
  return true;
}

// The one selector whose payload holds every source cell of a table cell
// line and whose column range (line-relative, origin cell == 0, like the
// block's border and separator columns) meets the cell's column range, or
// nullptr.  Positioned display columns are not used: the Layout IR can
// split a table line into several physical rows, each with its own column
// base (SC31-711 4.0 row 1).
std::vector<const ProseTableLinkIR*> line_links(
    const ProseTopicIR& prose, std::size_t span,
    const FixedTableBlockIR& block, std::size_t cell_index,
    const FixedTableCellLineIR& line) {
  const auto cell_begin = (cell_index == 0
                               ? block.left_column
                               : block.separator_columns[cell_index - 1]) + 1;
  const auto cell_end = cell_index < block.separator_columns.size()
                            ? block.separator_columns[cell_index]
                            : block.left_column + block.width;
  std::vector<const ProseTableLinkIR*> result;
  for (const auto& link : prose.table_links) {
    if (link.span != span) continue;
    if (link.column >= cell_end || link.column + link.length <= cell_begin)
      continue;
    bool covers = !line.source_cells.empty() || !line.unpositioned_cells.empty();
    const auto owned = [&](std::uint32_t record, std::size_t token) {
      return record == link.logical_record &&
             std::binary_search(link.payload_tokens.begin(),
                                link.payload_tokens.end(), token);
    };
    for (const auto& cell : line.source_cells)
      covers = covers && owned(cell.logical_record, cell.token_index);
    for (const auto& cell : line.unpositioned_cells)
      covers = covers && owned(cell.logical_record, cell.token_index);
    if (!covers) continue;
    result.push_back(&link);
  }
  return result;
}

// Replaces the text of every fully selector-covered cell line of the lowered
// table rows by a cross reference to the selector target.  Rows correspond
// one to one to the block's body rows; the caption is a paragraph and is
// left as text.
bool link_table_cells(const ProseTopicIR& prose, std::size_t span,
                      const FixedTableBlockIR& block,
                      std::vector<BlockIR>& lowered, std::string* error) {
  const auto table = std::find_if(lowered.begin(), lowered.end(),
                                  [](const auto& candidate) {
                                    return std::holds_alternative<TableBlockIR>(
                                        candidate.node);
                                  });
  if (table == lowered.end()) return fail(error, "table span has no table");
  auto& rows = std::get<TableBlockIR>(table->node).rows;
  if (rows.size() != block.body.size())
    return fail(error, "lowered table rows differ from the block rows");
  for (std::size_t row = 0; row < rows.size(); ++row) {
    if (rows[row].cells.size() != block.body[row].cells.size())
      return fail(error, "lowered table cells differ from the block cells");
    for (std::size_t cell = 0; cell < rows[row].cells.size(); ++cell) {
      auto& content = rows[row].cells[cell].content;
      const auto& lines = block.body[row].cells[cell].lines;
      // Lines alternate with hard breaks: text, break, text, ...
      if (content.size() != (lines.empty() ? 0 : lines.size() * 2 - 1))
        return fail(error, "lowered table cell lines differ from the block");
      for (std::size_t line = 0; line < lines.size(); ++line) {
        auto& node = content[line * 2];
        const auto* text = std::get_if<TextInlineIR>(&node.node);
        if (text == nullptr)
          return fail(error, "lowered table cell line is not text");
        const auto links = line_links(prose, span, block, cell, lines[line]);
        if (links.empty()) continue;
        const auto picture_links = std::count_if(
            links.begin(), links.end(), [](const auto* link) {
              return link->picture;
            });
        if (picture_links != 0 &&
            (picture_links != 1 || links.size() != 1))
          return fail(error,
                      "fixed table picture selector overlaps another "
                      "selector");
        // A picture selector replaces the cell line that spells its
        // `PICTURE n` placeholder with the image itself, which is what
        // hosted BookServer serves there.
        if (links.size() == 1 && links.front()->picture) {
          const auto* link = links.front();
          ImageInlineIR image;
          image.resource = "resource:" + link->target;
          image.alt_text = figure_picture_placeholder(link->target);
          if (text->text != image.alt_text)
            return fail(error, "picture cell line does not spell '" +
                                   image.alt_text + "'");
          node.node = std::move(image);
          node.origin.detail = "fixed table cell line (CSELECT picture)";
          node.origin.slices.push_back(link->source);
          std::sort(node.origin.slices.begin(), node.origin.slices.end(),
                    [](const auto& left, const auto& right) {
                      return std::make_tuple(left.logical_record,
                                             left.segment_index,
                                             left.token_begin, left.token_end) <
                             std::make_tuple(right.logical_record,
                                             right.segment_index,
                                             right.token_begin,
                                             right.token_end);
                    });
          continue;
        }
        CrossReferenceInlineIR reference;
        for (const auto* link : links)
          reference.targets.push_back(
              {prose_target(link->target_kind, link->target,
                            link->target_alternatives),
               origin({link->source}, "fixed table CSELECT target")});
        reference.label = text->text;
        node.node = std::move(reference);
        node.origin.detail =
            links.front()->target_kind == CrossReferenceTargetKindIR::external
                ? "fixed table cell line (CSELECT LNK reference)"
                : "fixed table cell line (CSELECT reference)";
      }
    }
  }
  return true;
}


// Wraps each maximal run of inlines from rows with one revision mark in a
// `RevisedInlineIR` (`content[first..]`, one mark per inline).  The run's
// origin covers its children's sources.
void wrap_revised_runs(InlineSequenceIR& content, std::size_t first,
                       const std::vector<char32_t>& marks) {
  if (std::all_of(marks.begin(), marks.end(),
                  [](const char32_t mark) { return mark == 0; }))
    return;
  InlineSequenceIR rebuilt(content.begin(),
                           content.begin() + static_cast<std::ptrdiff_t>(first));
  for (std::size_t index = 0; index < marks.size();) {
    auto& node = content[first + index];
    if (marks[index] == 0) {
      rebuilt.push_back(std::move(node));
      ++index;
      continue;
    }
    RevisedInlineIR revised;
    revised.mark = marks[index];
    std::vector<DocumentSourceSliceIR> slices;
    auto at = index;
    for (; at < marks.size() && marks[at] == marks[index]; ++at) {
      auto& child = content[first + at];
      slices.insert(slices.end(), child.origin.slices.begin(),
                    child.origin.slices.end());
      revised.children.push_back(std::move(child));
    }
    // The run owns its children's tokens whole, in source order and once
    // each, as a container origin does.
    for (auto& slice : slices) {
      slice.character_begin = 0;
      slice.character_end = 0;
    }
    const auto key = [](const DocumentSourceSliceIR& slice) {
      return std::make_tuple(slice.logical_record, slice.segment_index,
                             slice.token_begin, slice.token_end,
                             slice.byte_begin, slice.byte_end);
    };
    std::sort(slices.begin(), slices.end(),
              [&](const auto& left, const auto& right) {
                return key(left) < key(right);
              });
    slices.erase(std::unique(slices.begin(), slices.end(),
                             [&](const auto& left, const auto& right) {
                               return key(left) == key(right);
                             }),
                 slices.end());
    InlineIR wrapper;
    wrapper.node = std::move(revised);
    wrapper.origin = origin(std::move(slices), "prose revised rows");
    rebuilt.push_back(std::move(wrapper));
    index = at;
  }
  content = std::move(rebuilt);
}

bool lower_inlines(const ProseBlockIR& block, std::size_t begin,
                   std::size_t end, InlineSequenceIR& content,
                   std::string* error, bool flatten_emphasis = false) {
  std::vector<DocumentSourceSliceIR> imaged;
  const auto first = content.size();
  // The revision mark of every inline appended below, in order.
  std::vector<char32_t> marks;
  for (auto index = begin; index < end && index < block.inlines.size();
       ++index) {
    const auto& node = block.inlines[index];
    InlineIR lowered;
    if (flatten_emphasis && node.kind == ProseInlineKindIR::emphasis &&
        node.style != FontStyleIR::underscore) {
      lowered.node = TextInlineIR{node.text};
      lowered.origin = origin(node.slices, "prose CZ highlighted label");
      if (node.text.empty()) return fail(error, "prose inline has no text");
      content.push_back(std::move(lowered));
      marks.push_back(node.revision_mark);
      continue;
    }
    switch (node.kind) {
    case ProseInlineKindIR::text:
      lowered.node = TextInlineIR{node.text};
      lowered.origin = origin(node.slices, "prose text");
      break;
    case ProseInlineKindIR::emphasis:
      if (node.style == FontStyleIR::unknown)
        return fail(error, "prose emphasis has no highlight style");
      if (code_style(node.style)) {
        lowered.node = CodeInlineIR{node.text, phrase_kind(node.style)};
        lowered.origin = origin(node.slices, "prose CFONT example phrase");
      } else if (node.style == FontStyleIR::underscore) {
        // `_` is hosted `<u>`, not HP5's emphasis: SC24-5527-02 COMMENTS
        // (DT 19921218151459) serves `<u>___</u><B>YES</B>`.
        lowered.node = UnderlineInlineIR{node.text};
        lowered.origin = origin(node.slices, "prose CFONT underscore");
      } else {
        lowered.node = EmphasisInlineIR{node.text, emphasis_kind(node.style),
                                        phrase_kind(node.style)};
        lowered.origin = origin(node.slices, "prose CFONT highlight");
      }
      break;
    case ProseInlineKindIR::image: {
      if (node.target.empty())
        return fail(error, "prose inline picture has no resource");
      // Hosted BookServer replaces exactly the `PICTURE n` placeholder
      // words with the `<img>` and keeps them as its `alt` text.
      ImageInlineIR image;
      image.resource = "resource:" + node.target;
      image.alt_text = node.text;
      lowered.node = std::move(image);
      lowered.origin = origin(node.slices, "prose CSELECT picture");
      break;
    }
    case ProseInlineKindIR::cross_reference: {
      if (node.targets.empty())
        return fail(error, "prose cross-reference has no target");
      CrossReferenceInlineIR reference;
      reference.label = node.text;
      for (const auto& target : node.targets)
        reference.targets.push_back(
            {prose_target(target.kind, target.value, target.alternatives),
             origin({target.source}, "prose CSELECT target")});
      // A `LNK <IMAGE>` selector draws its image once, at the start of the
      // columns it selects; a label split into several runs (a font change
      // inside it) carries the image on its first run only.  The image is
      // drawn inside one link, so it may not share the label with another
      // selector claim.
      for (const auto& target : node.targets) {
        const auto image = target.kind == CrossReferenceTargetKindIR::external
                               ? selector_link_image(target.alternatives)
                               : std::string{};
        if (image.empty()) continue;
        if (node.targets.size() != 1)
          return fail(error, "image link label carries several selector "
                             "claims");
        const auto same_selector = [&](const DocumentSourceSliceIR& seen) {
          return seen.logical_record == target.source.logical_record &&
                 seen.segment_index == target.source.segment_index &&
                 seen.token_begin == target.source.token_begin &&
                 seen.token_end == target.source.token_end;
        };
        if (std::any_of(imaged.begin(), imaged.end(), same_selector)) continue;
        imaged.push_back(target.source);
        reference.label_image = ImageInlineIR{image, image};
      }
      // The prose family never lets `_` share a cell with a selector.
      if (node.style == FontStyleIR::underscore)
        return fail(error, "prose cross-reference label is underscored");
      if (code_style(node.style))
        reference.label_code = true;
      else if (node.style != FontStyleIR::unknown)
        reference.label_emphasis = emphasis_kind(node.style);
      reference.label_phrase = phrase_kind(node.style);
      lowered.node = std::move(reference);
      lowered.origin =
          origin(node.slices,
                 node.target_kind == CrossReferenceTargetKindIR::external
                     ? "prose CSELECT LNK reference"
                     : "prose CSELECT reference");
      break;
    }
    case ProseInlineKindIR::line_break:
      // The title-page projection keeps its display-row boundaries inside
      // one paragraph; hosted BookServer serves the rows one per line.
      lowered.node = HardBreakInlineIR{};
      lowered.origin = origin({}, "prose title-page row break");
      content.push_back(std::move(lowered));
      marks.push_back(node.revision_mark);
      continue;
    }
    if (node.text.empty()) return fail(error, "prose inline has no text");
    content.push_back(std::move(lowered));
    marks.push_back(node.revision_mark);
  }
  wrap_revised_runs(content, first, marks);
  return !content.empty() || fail(error, "prose block has no inlines");
}

bool lower_inlines(const ProseBlockIR& block, InlineSequenceIR& content,
                   std::string* error) {
  return lower_inlines(block, block.term_inline_count, block.inlines.size(),
                       content, error);
}

std::vector<DocumentSourceSliceIR> inline_slices(const ProseBlockIR& block,
                                                 std::size_t begin,
                                                 std::size_t end) {
  std::vector<DocumentSourceSliceIR> slices;
  for (auto index = begin; index < end && index < block.inlines.size(); ++index)
    slices.insert(slices.end(), block.inlines[index].slices.begin(),
                  block.inlines[index].slices.end());
  return slices;
}

// A CZ ordinal label ends in `.` (`1.`, `a.`) or `)` (`7)`, `h)`).
bool label_terminator(const std::string& label) {
  return !label.empty() && (label.back() == '.' || label.back() == ')');
}

OrderedListMarkerIR item_marker(const ProseBlockIR& block) {
  const auto& label = block.ordinal;
  if (label.size() == 2 && label_terminator(label) && label.front() >= 'a' &&
      label.front() <= 'z')
    return OrderedListMarkerIR::lower_alpha;
  if (label.size() == 2 && label_terminator(label) && label.front() >= 'A' &&
      label.front() <= 'Z')
    return OrderedListMarkerIR::upper_alpha;
  return OrderedListMarkerIR::decimal;
}

// The explicit ordinal label of a CZ ordered-list row, or the item's position
// in its list when the row has no label.
std::optional<std::uint64_t> item_ordinal(const ProseBlockIR& block,
                                          std::uint64_t position) {
  if (!block.ordered) return std::nullopt;
  const auto& label = block.ordinal;
  if (label.size() >= 2 && label_terminator(label) &&
      std::all_of(label.begin(), label.end() - 1, [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    std::uint64_t value = 0;
    for (auto it = label.begin(); it + 1 != label.end(); ++it)
      value = value * 10 + static_cast<std::uint64_t>(*it - '0');
    if (value != 0) return value;
  }
  if (label.size() == 2 && label_terminator(label) &&
      std::isalpha(static_cast<unsigned char>(label.front())) != 0)
    return static_cast<std::uint64_t>(
               std::tolower(static_cast<unsigned char>(label.front())) - 'a') +
           1;
  return position;
}

} // namespace

std::optional<InlineSequenceIR> lower_prose_inline_content(
    const std::vector<ProseInlineIR>& inlines, std::string* error) {
  ProseBlockIR block;
  block.inlines = inlines;
  InlineSequenceIR result;
  if (!lower_inlines(block, 0, inlines.size(), result, error))
    return std::nullopt;
  return result;
}

std::optional<DocumentIR> lower_prose_topic_to_document_ir(
    TopicIdentityIR identity, const ProseTopicIR& prose, std::string* error) {
  if (prose.heading_level.size() != 2 || prose.heading_level.front() != 'h' ||
      prose.heading_level.back() < '1' || prose.heading_level.back() > '6') {
    fail(error, "prose topic heading is incomplete");
    return std::nullopt;
  }
  // The source-proven CHDLEVEL is authoritative over compatibility metadata.
  identity.heading_level = prose.heading_level;

  DocumentIR document;
  document.topic = std::move(identity);
  const auto level =
      static_cast<std::uint32_t>(prose.heading_level.back() - '0');
  auto heading_origin = origin({prose.title_source}, "prose heading");
  // An `ST` control with an empty payload leaves the heading to the topic
  // identity prefix alone, which is what hosted BookServer serves
  // (`<H3> 8.1.1.1 </H3>`, SC09-138 DT 19910321130500).
  std::vector<InlineIR> heading_content;
  if (prose.title.empty()) {
    InlineIR identity_only;
    identity_only.node = TextInlineIR{document.topic.id};
    identity_only.origin.derivation = DocumentDerivationIR::synthesized;
    identity_only.origin.detail = "public topic identity prefix";
    heading_content.push_back(std::move(identity_only));
  } else {
    heading_content.push_back(
        InlineIR{TextInlineIR{prose.title}, heading_origin});
  }
  document.blocks.push_back(
      {HeadingBlockIR{level, std::move(heading_content)}, heading_origin});

  // Table and figure spans lower through their own typed blocks; the prose
  // family only places them.
  std::size_t emitted_spans = 0;
  std::string span_error;
  const auto emit_span = [&](const ProseSpanIR& span,
                             BlockSequenceIR& destination) -> bool {
    if (span.kind == ProseSpanKindIR::table) {
      if (span.index >= prose.tables.blocks.size())
        return fail(&span_error, "table span addresses no table block");
      const auto& table = prose.tables.blocks[span.index];
      auto blocks = lower_fixed_table_block_to_document_ir(table);
      if (blocks.empty()) return fail(&span_error, "table span lowered to nothing");
      const auto span_index =
          static_cast<std::size_t>(&span - prose.spans.data());
      // A verbatim region has no cells for a CSELECT to attach to: the table
      // block declines an envelope that carries one, and every envelope the
      // source did not declare a `:table` now renders as its display lines.
      // So the cell links apply only to a lowered Markdown table.
      const auto lowered_table =
          std::any_of(blocks.begin(), blocks.end(), [](const auto& candidate) {
            return std::holds_alternative<TableBlockIR>(candidate.node);
          });
      if (lowered_table &&
          !link_table_cells(prose, span_index, table, blocks, &span_error))
        return false;
      if ((!span.frame_before.empty() || !span.frame_after.empty()) &&
          !frame_table_rows(span, blocks, &span_error))
        return false;
      for (auto& block : blocks) destination.push_back(std::move(block));
    } else if (span.kind == ProseSpanKindIR::title) {
      // A further `ST` line heads the topic again, at its level and under
      // its number (hosted SC31-8212-00 2.3.3: `<H2> 2.3.3   Purpose</H2>`),
      // with the topic heading's own rule for a title that spells the id.
      if (span.index >= prose.repeated_titles.size())
        return fail(&span_error, "title span addresses no repeated ST title");
      const auto& repeated = prose.repeated_titles[span.index];
      if (repeated.title.empty())
        return fail(&span_error, "repeated ST title is empty");
      const auto repeated_origin =
          origin({repeated.source}, "prose repeated ST heading");
      HeadingBlockIR heading;
      heading.level = level;
      if (!ascii_equals_case_insensitive(trim_ascii(repeated.title),
                                         document.topic.id)) {
        InlineIR identity_prefix;
        identity_prefix.node = TextInlineIR{document.topic.id + " "};
        identity_prefix.origin.derivation = DocumentDerivationIR::synthesized;
        identity_prefix.origin.detail = "public topic identity prefix";
        heading.content.push_back(std::move(identity_prefix));
      }
      heading.content.push_back(
          InlineIR{TextInlineIR{repeated.title}, repeated_origin});
      destination.push_back({std::move(heading), repeated_origin});
    } else {
      if (span.index >= prose.figures.blocks.size())
        return fail(&span_error, "figure span addresses no figure block");
      auto blocks = lower_figure_block_to_document_blocks(
          prose.figures.blocks[span.index], &span_error);
      if (!blocks) return false;
      for (auto& block : *blocks) destination.push_back(std::move(block));
    }
    ++emitted_spans;
    return true;
  };
  // An anchor that opens a row inside a drawn box's block is lowered with
  // that block; every other anchor precedes the block at its position.
  constexpr auto npos_row = static_cast<std::size_t>(-1);
  // Anchors and spans placed at one position keep their source order: a span
  // follows the first `anchors_before` anchors of that position.
  const auto emit_anchors = [&](std::size_t position,
                                BlockSequenceIR& destination,
                                bool after_menu = false) -> bool {
    std::size_t ordinal = 0;
    const auto flush_spans = [&]() -> bool {
      for (const auto& span : prose.spans)
        if (span.position == position && span.anchors_before == ordinal &&
            !emit_span(span, destination))
          return false;
      return true;
    };
    if (!after_menu && !flush_spans()) return false;
    for (const auto& anchor : prose.anchors) {
      if (anchor.position != position || anchor.after_menu != after_menu ||
          anchor.row != npos_row)
        continue;
      destination.push_back(
          {AnchorBlockIR{anchor.id},
           origin({anchor.source}, "prose source anchor")});
      if (after_menu) continue;
      ++ordinal;
      if (!flush_spans()) return false;
    }
    return true;
  };

  const auto anchor_at = [&](std::size_t position) {
    return std::any_of(prose.anchors.begin(), prose.anchors.end(),
                       [&](const auto& anchor) {
                         return anchor.position == position &&
                                anchor.row == npos_row;
                       });
  };
  // A table or figure span placed between two items of one list splits the
  // list, exactly as an anchor does: the run below only visits the position
  // it stops at, so a span inside an unbroken run would never be emitted
  // (SC09-2417-00 6.2.3, where `cz OFF TABLE` stands between two
  // `cz FLOW LI` items of the same `cz FLOW UL`).
  const auto span_at = [&](std::size_t position) {
    return std::any_of(prose.spans.begin(), prose.spans.end(),
                       [&](const auto& span) {
                         return span.position == position;
                       });
  };
  // Items of one CZ list can be interleaved with paragraphs (a `cz FLOW P`
  // inside a list item); each run of items becomes one list block and the
  // ordinal keeps counting across the runs.
  std::map<std::size_t, std::uint64_t> list_positions;
  std::function<bool(const std::vector<std::size_t>&, BlockSequenceIR&)>
      lower_sequence;
  lower_sequence = [&](const std::vector<std::size_t>& indices,
                       BlockSequenceIR& destination) -> bool {
    std::size_t cursor = 0;
    while (cursor < indices.size()) {
      const auto block_index = indices[cursor];
      if (block_index >= prose.blocks.size())
        return fail(error, "compound prose child addresses no block");
      if (!emit_anchors(block_index, destination))
        return fail(error, "prose span rejected: " + span_error);
      const auto& block = prose.blocks[block_index];
      switch (block.kind) {
      case ProseBlockKindIR::paragraph: {
        ParagraphBlockIR paragraph;
        if (!lower_inlines(block, paragraph.content, error)) return false;
        destination.push_back(
            {std::move(paragraph), origin(block.slices, "prose paragraph")});
        ++cursor;
        continue;
      }
      case ProseBlockKindIR::thematic_break:
        destination.push_back(
            {ThematicBreakBlockIR{},
             origin(block.slices, "prose native thematic break")});
        ++cursor;
        continue;
      case ProseBlockKindIR::heading: {
        HeadingBlockIR heading;
        heading.level = static_cast<std::uint32_t>(block.heading_level);
        if (heading.level < 2 || heading.level > 6)
          return fail(error, "prose heading block level is outside 2..6");
        if (!lower_inlines(block, heading.content, error)) return false;
        destination.push_back(
            {std::move(heading), origin(block.slices, "prose CZ heading")});
        ++cursor;
        continue;
      }
      case ProseBlockKindIR::note: {
        NoteBlockIR note;
        std::string label;
        for (std::size_t inline_index = 0;
             inline_index < block.term_inline_count; ++inline_index)
          label += block.inlines[inline_index].text;
        while (!label.empty() && (label.back() == ':' || label.back() == ' '))
          label.pop_back();
        if (label.empty()) return fail(error, "prose note block has no label");
        note.label.push_back(
            {TextInlineIR{label},
             origin(inline_slices(block, 0, block.term_inline_count),
                    "prose CZ note label")});
        if (!lower_inlines(block, note.content, error)) return false;
        destination.push_back(
            {std::move(note), origin(block.slices, "prose CZ note")});
        ++cursor;
        continue;
      }
      case ProseBlockKindIR::preformatted: {
        if (block.preformatted_lines.empty())
          return fail(error, "prose preformatted block has no rows");
        auto preformatted_origin =
            origin(block.slices, "prose CZ example block");
        if (!block.verbatim_kind.empty())
          preformatted_origin.detail = "prose drawn box region: verbatim rows";
        PreformattedBlockIR preformatted;
        preformatted.lines = block.preformatted_lines;
        for (const auto& anchor : prose.anchors) {
          if (anchor.position != block_index || anchor.row == npos_row)
            continue;
          if (anchor.row >= preformatted.lines.size())
            return fail(error, "prose box anchor names no row of its block");
          preformatted.line_anchors.push_back(
              {anchor.row, AnchorBlockIR{anchor.id},
               origin({anchor.source}, "prose drawn box envelope anchor")});
        }
        if (!block.preformatted_line_inline_ends.empty()) {
          preformatted_origin.detail = "prose native styled example rows";
          std::size_t first = 0;
          std::size_t row_index = 0;
          for (const auto end : block.preformatted_line_inline_ends) {
            if (end < first || end > block.inlines.size() ||
                row_index >= block.preformatted_line_sources.size())
              return fail(error,
                          "styled preformatted row has invalid inline extent");
            InlineSequenceIR row;
            if (end > first && !lower_inlines(block, first, end, row, error))
              return false;
            preformatted.styled_lines.push_back(std::move(row));
            preformatted.line_origins.push_back(
                origin({block.preformatted_line_sources[row_index]},
                       "prose native styled display row"));
            first = end;
            ++row_index;
          }
        }
        if (!block.degradation_code.empty()) {
          preformatted_origin.detail =
              "prose CZ unmodelled region: verbatim rows";
          preformatted_origin.fidelity = DocumentFidelityIR::degraded;
          preformatted_origin.degradation_code = block.degradation_code;
          preformatted_origin.degradation_detail = block.degradation_detail;
          if (block.preformatted_line_inlines.size() ==
              block.preformatted_lines.size()) {
            for (const auto inline_index : block.preformatted_line_inlines) {
              DocumentNodeOriginIR line_origin;
              line_origin.derivation = DocumentDerivationIR::semantic_lowering;
              line_origin.fidelity = DocumentFidelityIR::degraded;
              line_origin.degradation_code = block.degradation_code;
              if (inline_index < block.inlines.size()) {
                line_origin.detail =
                    "prose CZ unmodelled region: display row";
                line_origin.slices = block.inlines[inline_index].slices;
              } else {
                line_origin.detail = "prose CZ unmodelled region: blank row";
              }
              preformatted.line_origins.push_back(std::move(line_origin));
            }
          }
        }
        destination.push_back(
            {std::move(preformatted), std::move(preformatted_origin)});
        ++cursor;
        continue;
      }
      case ProseBlockKindIR::footnote: {
        if (block.anchor_id.empty())
          return fail(error, "prose footnote block has no anchor");
        destination.push_back(
            {AnchorBlockIR{block.anchor_id, AnchorRoleIR::local},
             origin(block.slices, "prose CZ footnote anchor")});
        ParagraphBlockIR paragraph;
        if (!lower_inlines(block, paragraph.content, error)) return false;
        destination.push_back(
            {std::move(paragraph), origin(block.slices, "prose CZ footnote")});
        ++cursor;
        continue;
      }
      case ProseBlockKindIR::definition_entry: {
        DefinitionListBlockIR list;
        std::vector<DocumentSourceSliceIR> list_slices;
        const auto ordinal = block.list_ordinal;
        while (cursor < indices.size()) {
          const auto entry_index = indices[cursor];
          const auto& entry = prose.blocks[entry_index];
          if (entry.kind != ProseBlockKindIR::definition_entry ||
              entry.list_ordinal != ordinal)
            break;
          DefinitionEntryIR lowered;
          if (entry.term_inline_count == 0) {
            if (!entry.inlines.empty())
              return fail(error,
                          "empty prose definition term carries inline text");
            lowered.empty_term = true;
          } else if (!lower_inlines(entry, 0, entry.term_inline_count,
                                    lowered.term, error)) {
            return false;
          }
          const auto has_inline_body =
              entry.term_inline_count < entry.inlines.size();
          if (entry.body_blocks.empty()) {
            // A term alone: the CZ term whose body never came
            // (prose_topic_cz.cpp `term_alone`) lowers with no definition.
            if (!has_inline_body) {
              if (lowered.empty_term)
                return fail(error, "prose definition has no body");
            } else if (!lower_inlines(entry, lowered.definition, error)) {
              return fail(error, "prose definition has no body");
            }
          } else {
            if (has_inline_body) {
              ParagraphBlockIR paragraph;
              if (!lower_inlines(entry, paragraph.content, error)) return false;
              lowered.definition_blocks.push_back(
                  {std::move(paragraph),
                   origin(inline_slices(entry, entry.term_inline_count,
                                        entry.inlines.size()),
                          "prose CZ definition paragraph")});
            }
            if (!lower_sequence(entry.body_blocks,
                                lowered.definition_blocks))
              return false;
          }
          lowered.origin = origin(entry.slices, "prose CZ definition entry");
          list_slices.insert(list_slices.end(), entry.slices.begin(),
                             entry.slices.end());
          list.entries.push_back(std::move(lowered));
          ++cursor;
          if (cursor < indices.size() &&
              (anchor_at(indices[cursor]) || span_at(indices[cursor])))
            break;
        }
        destination.push_back(
            {std::move(list),
             origin(std::move(list_slices), "prose CZ definition list")});
        continue;
      }
      case ProseBlockKindIR::list_item: break;
      }
      ListBlockIR list;
      list.ordered = block.ordered;
      list.marker = item_marker(block);
      std::vector<DocumentSourceSliceIR> list_slices;
      const auto ordinal = block.list_ordinal;
      while (cursor < indices.size()) {
        const auto item_index = indices[cursor];
        const auto& item = prose.blocks[item_index];
        if (item.kind != ProseBlockKindIR::list_item ||
            item.list_ordinal != ordinal)
          break;
        if (item.ordered != list.ordered)
          return fail(error, "prose list mixes ordered and unordered items");
        if (item_marker(item) != list.marker)
          return fail(error,
                      "prose ordered list mixes marker alphabets");
        ListItemIR lowered;
        // An ordered item's kept head words (`1.` of `Step 1.`) are its
        // label; the list still numbers it by position.
        if (item.term_inline_count != 0 &&
            (!item.ordered || !item.ordinal.empty() ||
             !lower_inlines(item, 0, item.term_inline_count, lowered.label,
                            error)))
          return fail(error, "prose list item label is invalid");
        // A CZ item that owns no row (prose_topic_cz.cpp `list_item`) is
        // an empty item, or holds only the blocks that follow it.
        if (item.body_blocks.empty()) {
          if (item.inlines.empty())
            lowered.empty_content = true;
          else if (!lower_inlines(item, lowered.content, error))
            return false;
          // Every row of the item revised under one mark: the item carries
          // the mark (hosted ` | °   Defining the font size`, EFWUOMST 4.1),
          // and its content no wrapper of its own.
          if (lowered.label.empty() && lowered.content.size() == 1)
            if (auto* revised =
                    std::get_if<RevisedInlineIR>(&lowered.content.front().node)) {
              lowered.revision_mark = revised->mark;
              auto children = std::move(revised->children);
              lowered.content = std::move(children);
            }
        } else {
          if (!item.inlines.empty()) {
            ParagraphBlockIR paragraph;
            if (!lower_inlines(item, paragraph.content, error)) return false;
            lowered.blocks.push_back(
                {std::move(paragraph),
                 origin(item.slices, "prose list item paragraph")});
          }
          if (!lower_sequence(item.body_blocks, lowered.blocks)) return false;
        }
        lowered.origin = origin(item.slices, "prose list item");
        lowered.source_ordinal =
            item_ordinal(item, ++list_positions[ordinal]);
        list_slices.insert(list_slices.end(), item.slices.begin(),
                           item.slices.end());
        list.items.push_back(std::move(lowered));
        ++cursor;
        if (cursor < indices.size() &&
            (anchor_at(indices[cursor]) || span_at(indices[cursor])))
          break;
      }
      destination.push_back(
          {std::move(list), origin(std::move(list_slices), "prose list")});
    }
    return true;
  };

  std::vector<std::size_t> roots;
  for (std::size_t index = 0; index < prose.blocks.size(); ++index)
    if (prose.blocks[index].parent_block == static_cast<std::size_t>(-1))
      roots.push_back(index);
  if (!lower_sequence(roots, document.blocks)) return std::nullopt;
  if (!emit_anchors(prose.blocks.size(), document.blocks)) {
    fail(error, "prose span rejected: " + span_error);
    return std::nullopt;
  }
  if (emitted_spans != prose.spans.size()) {
    fail(error, "prose span was never placed");
    return std::nullopt;
  }

  if (!prose.menu_items.empty()) {
    MenuBlockIR menu;
    std::vector<DocumentSourceSliceIR> menu_slices;
    for (const auto& item : prose.menu_items) {
      if (item.target.empty() || item.label.empty()) {
        fail(error, "prose menu item is incomplete");
        return std::nullopt;
      }
      menu.items.push_back(
          {{CrossReferenceTargetKindIR::topic, item.target},
           item.label,
           origin({item.source}, "prose trailing menu item")});
      menu_slices.push_back(item.source);
    }
    document.blocks.push_back(
        {std::move(menu), origin(std::move(menu_slices), "prose trailing menu")});
  }
  emit_anchors(prose.blocks.size(), document.blocks, true);

  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid prose DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr) error->clear();
  return document;
}

bool verify_prose_topic_document_ir(const ProseTopicIR& prose,
                                    const DocumentIR& document,
                                    std::string* error) {
  auto expected =
      lower_prose_topic_to_document_ir(document.topic, prose, error);
  if (!expected) return false;
  if (format_document_ir(document) != format_document_ir(*expected))
    return fail(error, "prose DocumentIR differs from canonical lowering");
  if (error != nullptr) error->clear();
  return true;
}

} // namespace geist::detail
