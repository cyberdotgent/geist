#include "geist/detail/message_document_lowering.hpp"

#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace geist::detail {
namespace {

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

using SliceKey = std::tuple<std::uint32_t, std::size_t, std::size_t,
                            std::size_t, std::uint32_t, std::uint32_t>;

SliceKey slice_key(const DocumentSourceSliceIR &source) {
  return {source.logical_record, source.segment_index, source.token_begin,
          source.token_end,      source.byte_begin,    source.byte_end};
}

bool valid_slice(const DocumentSourceSliceIR &source) {
  return source.logical_record != 0 && source.token_begin <= source.token_end &&
         source.byte_begin <= source.byte_end;
}

DocumentNodeOriginIR origin(std::string detail) {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  result.detail = std::move(detail);
  return result;
}

void add_slice(DocumentNodeOriginIR &destination,
               const DocumentSourceSliceIR &source) {
  if (valid_slice(source))
    destination.slices.push_back(source);
}

void add_row(DocumentNodeOriginIR &destination,
             const DocumentSourceRowIR &source) {
  if (source.display_run != 0)
    destination.rows.push_back(source);
}

void canonicalize(DocumentNodeOriginIR &value) {
  std::sort(value.slices.begin(), value.slices.end(),
            [](const auto &left, const auto &right) {
              return slice_key(left) < slice_key(right);
            });
  value.slices.erase(std::unique(value.slices.begin(), value.slices.end(),
                                 [](const auto &left, const auto &right) {
                                   return slice_key(left) == slice_key(right);
                                 }),
                     value.slices.end());
  std::sort(value.rows.begin(), value.rows.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.display_run, left.row_index) <
                     std::tie(right.display_run, right.row_index);
            });
  value.rows.erase(std::unique(value.rows.begin(), value.rows.end(),
                               [](const auto &left, const auto &right) {
                                 return left.display_run == right.display_run &&
                                        left.row_index == right.row_index;
                               }),
                   value.rows.end());
}

void merge_origin(DocumentNodeOriginIR &destination,
                  const DocumentNodeOriginIR &source) {
  destination.slices.insert(destination.slices.end(), source.slices.begin(),
                            source.slices.end());
  destination.rows.insert(destination.rows.end(), source.rows.begin(),
                          source.rows.end());
  canonicalize(destination);
}

DocumentNodeOriginIR slice_origin(const DocumentSourceSliceIR &source,
                                  std::string detail) {
  auto result = origin(std::move(detail));
  add_slice(result, source);
  canonicalize(result);
  return result;
}

const MessageTopicSegmentIR *find_segment(const MessageTopicIR &message,
                                          std::uint32_t logical_record,
                                          std::size_t segment_index) {
  const auto found = std::find_if(
      message.segments.begin(), message.segments.end(), [&](const auto &item) {
        return item.source.logical_record == logical_record &&
               item.source.segment_index == segment_index;
      });
  return found == message.segments.end() ? nullptr : &*found;
}

DocumentNodeOriginIR paragraph_origin(const MessageTopicIR &message,
                                      const MessageParagraphIR &paragraph,
                                      std::string detail) {
  auto result = origin(std::move(detail));
  for (const auto &row : paragraph.source_rows)
    add_row(result, {row.first, row.second});
  for (const auto &coordinate : paragraph.source_segments) {
    const auto *segment =
        find_segment(message, coordinate.first, coordinate.second);
    if (segment != nullptr)
      add_slice(result, segment->source);
  }
  for (const auto &source : paragraph.source_slices)
    add_slice(result, source);
  canonicalize(result);
  return result;
}

DocumentNodeOriginIR
introduction_atom_origin(const MessageTopicIR &message,
                         const MessageIntroductionAtomIR &atom,
                         std::string detail) {
  auto result = origin(std::move(detail));
  for (const auto cell_index : atom.cell_indices) {
    // verify_message_shape proves these lookups before lowering. Keeping the
    // construction total here avoids a second, subtly different fallback.
    const auto &cell = message.introduction.cells[cell_index];
    const auto token =
        std::find_if(message.source_tokens.begin(), message.source_tokens.end(),
                     [&](const auto &item) {
                       return item.logical_record == cell.logical_record &&
                              item.token_index == cell.token_index;
                     });
    const auto segment_index = *token->decoded_segment;
    result.slices.push_back({token->logical_record, segment_index,
                             token->token_index, token->token_index + 1,
                             token->bytes.begin, token->bytes.end});
  }
  canonicalize(result);
  return result;
}

bool source_proven(const MessageParagraphIR &paragraph) {
  return !paragraph.text.empty() && (!paragraph.source_rows.empty() ||
                                     !paragraph.source_segments.empty() ||
                                     !paragraph.source_slices.empty());
}

bool paragraph_coordinates_exist(const MessageTopicIR &message,
                                 const MessageParagraphIR &paragraph) {
  const auto segments =
      std::all_of(paragraph.source_segments.begin(),
                  paragraph.source_segments.end(), [&](const auto &coordinate) {
                    return find_segment(message, coordinate.first,
                                        coordinate.second) != nullptr;
                  });
  const auto slices = std::all_of(
      paragraph.source_slices.begin(), paragraph.source_slices.end(),
      [&](const auto &slice) {
        if (!valid_slice(slice) || slice.token_begin >= slice.token_end)
          return false;
        return std::all_of(message.source_tokens.begin(),
                           message.source_tokens.end(), [&](const auto &token) {
                             if (token.logical_record != slice.logical_record ||
                                 token.token_index < slice.token_begin ||
                                 token.token_index >= slice.token_end)
                               return true;
                             return token.bytes.begin >= slice.byte_begin &&
                                    token.bytes.end <= slice.byte_end;
                           });
      });
  return segments && slices;
}

bool verify_message_shape(const MessageTopicIR &message, std::string *error) {
  if (message.first_logical_record == 0 ||
      message.first_logical_record >= message.end_logical_record ||
      message.metadata.raw_topic_id.empty() || message.title.empty() ||
      message.metadata.heading_level.size() != 2 ||
      (message.metadata.heading_level.front() != 'H' &&
       message.metadata.heading_level.front() != 'h') ||
      message.metadata.heading_level.back() < '1' ||
      message.metadata.heading_level.back() > '6' ||
      message.heading_row_indices.empty() ||
      message.introduction.paragraphs.empty() ||
      message.catalog.entries.empty() ||
      message.anchors.size() != message.catalog.entries.size() + 2 ||
      message.segments.empty() || message.source_tokens.empty() ||
      !valid_slice(message.terminal_content_source))
    return fail(error, "message topic lowering envelope is incomplete");

  for (const auto row_index : message.heading_row_indices)
    if (row_index >= message.rows.size())
      return fail(error, "message heading references an invalid source row");

  if (message.anchors[0].id != "MSG" || message.anchors[1].id != "HDRMSGS" ||
      !valid_slice(message.anchors[0].source) ||
      !valid_slice(message.anchors[1].source))
    return fail(error, "message topic source anchors are incomplete");

  const auto heading_segment = std::find_if(
      message.segments.begin(), message.segments.end(), [](const auto &item) {
        return item.role == MessageTopicSegmentRoleIR::heading;
      });
  if (heading_segment == message.segments.end() ||
      slice_key(message.anchors[0].source) >=
          slice_key(message.anchors[1].source) ||
      slice_key(message.anchors[1].source) >=
          slice_key(heading_segment->source))
    return fail(error, "message header semantics are not source ordered");

  std::vector<std::size_t> claims(message.introduction.cells.size());
  for (const auto &paragraph : message.introduction.paragraphs) {
    if (paragraph.atoms.empty())
      return fail(error, "message introduction has an empty paragraph");
    for (const auto &atom : paragraph.atoms) {
      if (atom.text.empty())
        return fail(error, "message introduction has an empty atom");
      if (atom.kind == MessageIntroductionAtomKindIR::selector) {
        if (!atom.target || atom.target->value.empty())
          return fail(error, "message introduction selector has no target");
      } else if (atom.target) {
        return fail(error, "message introduction text atom has a target");
      }
      for (const auto cell : atom.cell_indices) {
        if (cell >= claims.size())
          return fail(error, "message introduction atom has an invalid cell");
        ++claims[cell];
      }
    }
  }
  for (std::size_t cell = 0; cell < claims.size(); ++cell) {
    const auto role = message.introduction.cells[cell].role;
    const auto semantic = role == MessageIntroductionCellRoleIR::text ||
                          role == MessageIntroductionCellRoleIR::selector;
    if (claims[cell] != (semantic ? 1u : 0u))
      return fail(error,
                  "message introduction source-cell claims are not exact");
    if (!semantic)
      continue;
    const auto &source_cell = message.introduction.cells[cell];
    const auto token = std::find_if(
        message.source_tokens.begin(), message.source_tokens.end(),
        [&](const auto &item) {
          return item.logical_record == source_cell.logical_record &&
                 item.token_index == source_cell.token_index;
        });
    if (token == message.source_tokens.end() || !token->decoded_segment ||
        find_segment(message, source_cell.logical_record,
                     *token->decoded_segment) == nullptr)
      return fail(error,
                  "message introduction cell lacks decoded source provenance");
  }

  for (std::size_t index = 0; index < message.catalog.entries.size(); ++index) {
    const auto &entry = message.catalog.entries[index];
    const auto &anchor = message.anchors[index + 2];
    if (entry.id.empty() || anchor.id != "MSG " + entry.id ||
        !valid_slice(anchor.source) || !source_proven(entry.headline) ||
        !paragraph_coordinates_exist(message, entry.headline) ||
        entry.sections.size() != 2 ||
        entry.sections[0].kind != MessageSectionKind::meaning ||
        entry.sections[1].kind != MessageSectionKind::action)
      return fail(error, "message catalog entry semantics are incomplete");
    if (index != 0 && slice_key(message.anchors[index + 1].source) >=
                          slice_key(anchor.source))
      return fail(error, "message catalog anchors are not source ordered");
    for (const auto &continuation : entry.headline_continuations)
      if (!source_proven(continuation) ||
          !paragraph_coordinates_exist(message, continuation))
        return fail(error,
                    "message headline continuation lacks source provenance");
    for (const auto &section : entry.sections) {
      if (section.paragraphs.empty() || section.label_source_slices.empty() ||
          !std::all_of(section.label_source_slices.begin(),
                       section.label_source_slices.end(), valid_slice))
        return fail(error, "message section has no paragraphs");
      for (const auto &paragraph : section.paragraphs)
        if (!source_proven(paragraph) ||
            !paragraph_coordinates_exist(message, paragraph))
          return fail(
              error,
              "message " + entry.id + " " +
                  (section.kind == MessageSectionKind::meaning ? "Meaning"
                                                               : "Action") +
                  " paragraph lacks source provenance at " +
                  (paragraph.source_segments.empty()
                       ? std::string{"row-only paragraph"}
                       : std::to_string(
                             paragraph.source_segments.front().first) +
                             ":" +
                             std::to_string(
                                 paragraph.source_segments.front().second)));
    }
  }
  return true;
}

BlockIR paragraph_block(InlineSequenceIR content,
                        DocumentNodeOriginIR block_origin) {
  return {ParagraphBlockIR{std::move(content)}, std::move(block_origin)};
}

std::string compact(std::string value) {
  return collapse_ascii_whitespace(trim_ascii(std::move(value)));
}

void append_text(std::string &destination, const std::string &text) {
  const auto value = compact(text);
  if (value.empty())
    return;
  if (!destination.empty())
    destination.push_back(' ');
  destination += value;
}

using TokenKey = std::pair<std::uint32_t, std::size_t>;
using SourceTokenIndex = std::map<TokenKey, const MessageTopicSourceTokenIR *>;

SourceTokenIndex index_source_tokens(const MessageTopicIR &message) {
  SourceTokenIndex result;
  for (const auto &token : message.source_tokens)
    result[{token.logical_record, token.token_index}] = &token;
  return result;
}

// Exact token/byte provenance for positioned source cells claimed by a
// structured block. Every claimed cell must resolve to a decoded payload
// token of this topic; a cell outside the topic ledger is a lowering error,
// not something to render without provenance.
bool add_cell_slices(const SourceTokenIndex &tokens,
                     const std::vector<MessageStructuredSourceCellIR> &cells,
                     DocumentNodeOriginIR &destination) {
  for (const auto &cell : cells) {
    const auto token = tokens.find({cell.logical_record, cell.token_index});
    if (token == tokens.end() || !token->second->decoded_segment)
      return false;
    destination.slices.push_back(
        {cell.logical_record, *token->second->decoded_segment,
         cell.token_index, cell.token_index + 1, token->second->bytes.begin,
         token->second->bytes.end});
  }
  return true;
}

void add_source_rows(const std::vector<MessageSourceRowIR> &rows,
                     DocumentNodeOriginIR &destination) {
  for (const auto &row : rows)
    add_row(destination, {row.first, row.second});
}

struct SectionBlockClaims {
  // Claimed physical rows in block order (first appearance).
  std::vector<MessageSourceRowIR> rows;
  // Cells the block retains as structural evidence only (no rendered text).
  std::set<TokenKey> structural_tokens;
};

SectionBlockClaims block_claims(const MessageSectionBlockIR &block) {
  SectionBlockClaims claims;
  std::set<MessageSourceRowIR> seen;
  const auto claim_rows = [&](const std::vector<MessageSourceRowIR> &rows) {
    for (const auto &row : rows)
      if (seen.insert(row).second)
        claims.rows.push_back(row);
  };
  const auto claim_structural =
      [&](const std::vector<MessageStructuredSourceCellIR> &cells) {
        for (const auto &cell : cells)
          claims.structural_tokens.insert(
              {cell.logical_record, cell.token_index});
      };
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, MessageStructuredTableBlockIR>) {
          const auto visit_row = [&](const MessageStructuredTableRowIR &row) {
            for (const auto &cell : row.cells)
              claim_rows(cell.source_rows);
            claim_structural(row.structural_cells);
          };
          visit_row(node.header);
          for (const auto &row : node.rows)
            visit_row(row);
        } else if constexpr (std::is_same_v<T, MessageStructuredListBlockIR>) {
          claim_rows(node.lead_in.source_rows);
          for (const auto &item : node.items) {
            claim_rows(item.source_rows);
            claim_structural(item.structural_cells);
          }
        } else {
          for (const auto &line : node.lines)
            claim_rows({line.source_row});
        }
      },
      block.node);
  return claims;
}

const MessageTopicRowIR *find_topic_row(const MessageTopicIR &message,
                                        const MessageSourceRowIR &source) {
  for (const auto &row : message.rows)
    if (row.source_row.display_run == source.first &&
        row.source_row.row_index == source.second)
      return &row;
  return nullptr;
}

// The paragraph's text as the structured block accounts for it. A compact
// marker whose spelling message semantics carried into the row text, but
// which the block claims as a positioned structural cell, is exactly the
// marker's decoded text at the front of the paragraph. Nothing else is
// removed; any other disagreement fails the conservation check.
std::optional<std::string>
claimed_paragraph_text(const MessageTopicIR &message,
                       const MessageParagraphIR &paragraph,
                       const SectionBlockClaims &claims) {
  auto text = compact(paragraph.text);
  if (paragraph.semantic_rows.empty())
    return text;
  const auto &first = paragraph.semantic_rows.front();
  if (first.marker_disposition != MessageMarkerDispositionIR::lexical_prefix &&
      first.marker_disposition != MessageMarkerDispositionIR::list_prefix)
    return text;
  const auto *row = find_topic_row(message, first.source_row);
  if (row == nullptr || !row->marker ||
      claims.structural_tokens.count(
          {row->marker->logical_record, row->marker->token_index}) == 0)
    return text;
  const auto marker = compact(row->marker->decoded_text);
  if (text == marker)
    return std::string{};
  if (text.size() > marker.size() &&
      text.compare(0, marker.size(), marker) == 0 && text[marker.size()] == ' ')
    return text.substr(marker.size() + 1);
  return std::nullopt;
}

struct StructuredSectionLowering {
  std::string prose_before;
  DocumentNodeOriginIR prose_before_origin;
  std::optional<BlockIR> block;
  std::string prose_after;
  DocumentNodeOriginIR prose_after_origin;
};

// Lowers one message section around its verified structured block. The
// section's paragraphs stay in source order: paragraphs before the block's
// row span remain prose, the block's rows become the typed node, and
// paragraphs after the span remain prose. The union of rendered text is
// checked to equal the flattened section text so no source words are lost,
// duplicated, or invented by the structural interpretation.
std::optional<StructuredSectionLowering>
lower_structured_section(const MessageTopicIR &message,
                         const SourceTokenIndex &tokens,
                         const MessageSectionIR &section,
                         const MessageSectionBlockIR &block,
                         const std::string &entry_id, std::string *error) {
  const auto claims = block_claims(block);
  const auto reject = [&](const std::string &reason) {
    fail(error, "message " + entry_id + " structured section " + reason);
    return std::optional<StructuredSectionLowering>{};
  };
  if (claims.rows.empty())
    return reject("claims no source rows");

  // Map claimed rows to the paragraphs that own them.
  std::map<MessageSourceRowIR, std::size_t> owner;
  for (std::size_t index = 0; index < section.paragraphs.size(); ++index)
    for (const auto &row : section.paragraphs[index].semantic_rows)
      if (!owner.emplace(row.source_row, index).second)
        return reject("has a row owned by two paragraphs");
  const std::set<MessageSourceRowIR> claimed(claims.rows.begin(),
                                             claims.rows.end());
  std::optional<std::size_t> span_begin;
  std::optional<std::size_t> span_end;
  for (const auto &row : claims.rows) {
    const auto found = owner.find(row);
    if (found == owner.end())
      return reject("claims a row outside its section");
    span_begin =
        span_begin ? std::min(*span_begin, found->second) : found->second;
    span_end = span_end ? std::max(*span_end, found->second) : found->second;
  }
  std::vector<bool> paragraph_claimed(section.paragraphs.size(), false);
  for (std::size_t index = 0; index < section.paragraphs.size(); ++index) {
    const auto &rows = section.paragraphs[index].semantic_rows;
    const auto count = static_cast<std::size_t>(
        std::count_if(rows.begin(), rows.end(), [&](const auto &row) {
          return claimed.count(row.source_row) != 0;
        }));
    if (count != 0 && count != rows.size())
      return reject("claims part of a multi-row paragraph");
    paragraph_claimed[index] = count != 0;
  }
  const auto preformatted =
      std::holds_alternative<MessageStructuredPreformattedBlockIR>(block.node);
  for (auto index = *span_begin; index <= *span_end; ++index)
    if (!paragraph_claimed[index] && !preformatted)
      return reject("has unplaced prose inside its row span");

  // Expected text of the typed node in rendering order. Preformatted lines
  // interleave unclaimed source-ordered paragraphs (row-less recovered
  // fields) so nothing inside the span is dropped or reordered.
  std::vector<std::string> pieces;
  std::vector<std::string> lines;
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, MessageStructuredTableBlockIR>) {
          const auto visit_row = [&](const MessageStructuredTableRowIR &row) {
            for (const auto &cell : row.cells)
              pieces.push_back(compact(cell.text));
          };
          visit_row(node.header);
          for (const auto &row : node.rows)
            visit_row(row);
        } else if constexpr (std::is_same_v<T, MessageStructuredListBlockIR>) {
          pieces.push_back(compact(node.lead_in.text));
          for (const auto &item : node.items)
            pieces.push_back(compact(item.text));
        } else {
          for (auto index = *span_begin; index <= *span_end; ++index) {
            const auto &paragraph = section.paragraphs[index];
            if (!paragraph_claimed[index]) {
              lines.push_back(compact(paragraph.text));
              continue;
            }
            for (const auto &row : paragraph.semantic_rows)
              for (const auto &line : node.lines)
                if (line.source_row == row.source_row)
                  lines.push_back(compact(line.text));
          }
          pieces = lines;
        }
      },
      block.node);
  std::string block_text;
  for (const auto &piece : pieces)
    append_text(block_text, piece);
  if (block_text.empty())
    return reject("renders no text");

  std::string claimed_text;
  for (auto index = *span_begin; index <= *span_end; ++index) {
    const auto text =
        claimed_paragraph_text(message, section.paragraphs[index], claims);
    if (!text)
      return reject("carries a structural marker outside its row start");
    append_text(claimed_text, *text);
  }
  const auto at = claimed_text.find(block_text);
  if (at == std::string::npos ||
      claimed_text.find(block_text, at + 1) != std::string::npos ||
      (at != 0 && claimed_text[at - 1] != ' ') ||
      (at + block_text.size() < claimed_text.size() &&
       claimed_text[at + block_text.size()] != ' '))
    return reject("does not conserve the flattened section text");
  const auto prefix = compact(claimed_text.substr(0, at));
  const auto suffix = compact(claimed_text.substr(at + block_text.size()));

  StructuredSectionLowering result;
  result.prose_before_origin = origin("message section paragraph text");
  result.prose_after_origin = origin("message section paragraph text");
  const auto prose_origin = [&](std::size_t index) {
    return paragraph_origin(message, section.paragraphs[index],
                            "message section paragraph text");
  };
  for (std::size_t index = 0; index < *span_begin; ++index) {
    append_text(result.prose_before, section.paragraphs[index].text);
    merge_origin(result.prose_before_origin, prose_origin(index));
  }
  if (!prefix.empty()) {
    append_text(result.prose_before, prefix);
    merge_origin(result.prose_before_origin, prose_origin(*span_begin));
  }
  if (!suffix.empty()) {
    append_text(result.prose_after, suffix);
    merge_origin(result.prose_after_origin, prose_origin(*span_end));
  }
  for (auto index = *span_end + 1; index < section.paragraphs.size();
       ++index) {
    append_text(result.prose_after, section.paragraphs[index].text);
    merge_origin(result.prose_after_origin, prose_origin(index));
  }

  auto block_origin = origin("message structured section block");
  for (auto index = *span_begin; index <= *span_end; ++index)
    merge_origin(block_origin,
                 paragraph_origin(message, section.paragraphs[index],
                                  "message structured section block"));
  bool provenance = true;
  const auto cell_origin =
      [&](const std::vector<MessageSourceRowIR> &rows,
          const std::vector<MessageStructuredSourceCellIR> &cells,
          const char *detail) {
        auto cell_result = origin(detail);
        add_source_rows(rows, cell_result);
        provenance = provenance && add_cell_slices(tokens, cells, cell_result);
        canonicalize(cell_result);
        merge_origin(block_origin, cell_result);
        return cell_result;
      };
  const auto text_inline = [](const std::string &text,
                              const DocumentNodeOriginIR &node_origin) {
    return InlineIR{TextInlineIR{text}, node_origin};
  };
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, MessageStructuredTableBlockIR>) {
          TableBlockIR table;
          table.header_rows = 1;
          const auto lower_row = [&](const MessageStructuredTableRowIR &row) {
            TableRowIR target;
            target.origin = origin("message table row");
            for (const auto &cell : row.cells) {
              TableCellIR lowered;
              lowered.origin = cell_origin(cell.source_rows, cell.source_cells,
                                           "message table cell");
              const auto text = compact(cell.text);
              if (!text.empty())
                lowered.content.push_back(text_inline(text, lowered.origin));
              merge_origin(target.origin, lowered.origin);
              target.cells.push_back(std::move(lowered));
            }
            merge_origin(target.origin,
                         cell_origin({}, row.structural_cells,
                                     "message table structural cells"));
            table.rows.push_back(std::move(target));
          };
          lower_row(node.header);
          for (const auto &row : node.rows)
            lower_row(row);
          result.block = BlockIR{std::move(table), block_origin};
        } else if constexpr (std::is_same_v<T, MessageStructuredListBlockIR>) {
          // The lead-in sentence stays with the preceding prose paragraph;
          // its cells remain part of the block's provenance.
          const auto lead_origin =
              cell_origin(node.lead_in.source_rows, node.lead_in.source_cells,
                          "message list lead-in");
          append_text(result.prose_before, node.lead_in.text);
          merge_origin(result.prose_before_origin, lead_origin);
          ListBlockIR list;
          for (const auto &item : node.items) {
            auto item_origin = cell_origin(item.source_rows, item.source_cells,
                                           "message list item");
            merge_origin(item_origin,
                         cell_origin({}, item.structural_cells,
                                     "message list structural cells"));
            ListItemIR lowered;
            lowered.content.push_back(
                text_inline(compact(item.text), item_origin));
            lowered.origin = item_origin;
            list.items.push_back(std::move(lowered));
          }
          result.block = BlockIR{std::move(list), block_origin};
        } else {
          for (const auto &line : node.lines)
            cell_origin({line.source_row}, line.source_cells,
                        "message preformatted line");
          PreformattedBlockIR preformatted_block;
          preformatted_block.lines = lines;
          result.block = BlockIR{std::move(preformatted_block), block_origin};
        }
      },
      block.node);
  if (!provenance)
    return reject("cell lacks token provenance");
  canonicalize(block_origin);
  result.block->origin = block_origin;
  canonicalize(result.prose_before_origin);
  canonicalize(result.prose_after_origin);
  return result;
}

const MessageSectionBlockIR *
section_block(const MessageSectionBlocksIR &blocks, std::size_t entry_index,
              std::size_t section_index) {
  for (const auto &block : blocks.blocks)
    if (block.entry_index == entry_index &&
        block.section_index == section_index)
      return &block;
  return nullptr;
}

bool verify_blocks_shape(const MessageTopicIR &message,
                         const MessageSectionBlocksIR &blocks,
                         std::string *error) {
  std::set<std::pair<std::size_t, std::size_t>> owners;
  for (const auto &block : blocks.blocks) {
    if (block.entry_index >= message.catalog.entries.size() ||
        block.section_index >=
            message.catalog.entries[block.entry_index].sections.size())
      return fail(error, "message structured block owner is invalid");
    if (!owners.insert({block.entry_index, block.section_index}).second)
      return fail(error, "message section has more than one structured block");
  }
  return true;
}

std::optional<DocumentIR>
canonical_document(TopicIdentityIR topic, const MessageTopicIR &message,
                   const MessageSectionBlocksIR &blocks, std::string *error) {
  if (!verify_message_shape(message, error) ||
      !verify_blocks_shape(message, blocks, error))
    return std::nullopt;
  if ((!topic.id.empty() && topic.id != message.metadata.raw_topic_id) ||
      (topic.start_logical_record != 0 &&
       topic.start_logical_record != message.first_logical_record) ||
      (topic.end_logical_record != 0 &&
       topic.end_logical_record != message.end_logical_record)) {
    fail(error, "topic and message envelopes differ");
    return std::nullopt;
  }
  topic.id = message.metadata.raw_topic_id;
  topic.title = message.title;
  topic.heading_level = message.metadata.heading_level;
  topic.start_logical_record = message.first_logical_record;
  topic.end_logical_record = message.end_logical_record;

  DocumentIR document;
  document.topic = std::move(topic);
  const auto tokens = index_source_tokens(message);

  // Both named source boundaries precede ST, so they precede the heading in
  // the canonical document as well. Renderers must not rediscover this order.
  for (std::size_t index = 0; index < 2; ++index) {
    auto source = slice_origin(message.anchors[index].source,
                               "message topic source anchor");
    document.blocks.push_back(
        {AnchorBlockIR{message.anchors[index].id}, std::move(source)});
  }

  auto heading_origin = origin("message topic heading");
  for (const auto row_index : message.heading_row_indices) {
    add_slice(heading_origin, message.rows[row_index].source);
    add_row(heading_origin, message.rows[row_index].source_row);
  }
  canonicalize(heading_origin);
  const auto heading_level =
      static_cast<std::uint32_t>(message.metadata.heading_level.back() - '0');
  document.blocks.push_back(
      {HeadingBlockIR{heading_level,
                      {{TextInlineIR{message.title}, heading_origin}}},
       heading_origin});

  for (const auto &source_paragraph : message.introduction.paragraphs) {
    auto block_origin = origin("message topic introduction paragraph");
    InlineSequenceIR content;
    for (const auto &atom : source_paragraph.atoms) {
      auto atom_origin = introduction_atom_origin(
          message, atom, "message topic introduction atom");
      merge_origin(block_origin, atom_origin);
      if (atom.kind == MessageIntroductionAtomKindIR::selector)
        content.push_back({CrossReferenceInlineIR{*atom.target, atom.text},
                           std::move(atom_origin)});
      else
        content.push_back({TextInlineIR{atom.text}, std::move(atom_origin)});
    }
    document.blocks.push_back(
        paragraph_block(std::move(content), std::move(block_origin)));
  }

  for (std::size_t index = 0; index < message.catalog.entries.size(); ++index) {
    const auto &entry = message.catalog.entries[index];
    auto anchor_origin = slice_origin(message.anchors[index + 2].source,
                                      "message entry source anchor");
    document.blocks.push_back({AnchorBlockIR{message.anchors[index + 2].id},
                               std::move(anchor_origin)});

    auto headline_origin =
        paragraph_origin(message, entry.headline, "message entry headline");
    auto headline_text = entry.headline.text;
    for (const auto &continuation : entry.headline_continuations) {
      auto body_origin = paragraph_origin(
          message, continuation, "message entry headline continuation");
      merge_origin(headline_origin, body_origin);
      if (!headline_text.empty())
        headline_text.push_back(' ');
      headline_text += continuation.text;
    }
    canonicalize(headline_origin);
    const auto headline_inline_origin = headline_origin;
    // BookServer renders every headline word and the section labels as bold
    // runs; the typed document keeps that as strong emphasis.
    document.blocks.push_back(paragraph_block(
        {{EmphasisInlineIR{std::move(headline_text), EmphasisKindIR::strong},
          headline_inline_origin}},
        std::move(headline_origin)));

    for (std::size_t section_index = 0; section_index < entry.sections.size();
         ++section_index) {
      const auto &section = entry.sections[section_index];
      auto label_origin = origin("message section label");
      for (const auto &row : section.label_source_rows)
        add_row(label_origin, {row.first, row.second});
      for (const auto &slice : section.label_source_slices)
        add_slice(label_origin, slice);
      canonicalize(label_origin);
      const auto *label =
          section.kind == MessageSectionKind::meaning ? "Meaning:" : "Action:";

      std::string section_text;
      auto text_origin = origin("message section paragraph text");
      std::optional<StructuredSectionLowering> structured;
      const auto *block = section_block(blocks, index, section_index);
      if (block != nullptr) {
        structured = lower_structured_section(message, tokens, section, *block,
                                              entry.id, error);
        if (!structured)
          return std::nullopt;
        section_text = structured->prose_before;
        text_origin = structured->prose_before_origin;
      } else {
        for (std::size_t paragraph_index = 0;
             paragraph_index < section.paragraphs.size(); ++paragraph_index) {
          if (paragraph_index != 0)
            section_text.push_back(' ');
          const auto &paragraph = section.paragraphs[paragraph_index];
          merge_origin(text_origin,
                       paragraph_origin(message, paragraph,
                                        "message section paragraph text"));
          section_text += paragraph.text;
        }
      }

      auto block_origin = label_origin;
      merge_origin(block_origin, text_origin);
      InlineSequenceIR content;
      content.push_back({EmphasisInlineIR{label, EmphasisKindIR::strong},
                         std::move(label_origin)});
      if (!section_text.empty()) {
        auto separator_origin = origin("message section separator");
        separator_origin.derivation = DocumentDerivationIR::synthesized;
        content.push_back({TextInlineIR{" "}, std::move(separator_origin)});
        content.push_back({TextInlineIR{section_text}, text_origin});
      }
      document.blocks.push_back(
          paragraph_block(std::move(content), block_origin));
      if (!structured)
        continue;
      document.blocks.push_back(std::move(*structured->block));
      if (!structured->prose_after.empty())
        document.blocks.push_back(
            paragraph_block({{TextInlineIR{structured->prose_after},
                              structured->prose_after_origin}},
                            structured->prose_after_origin));
    }
  }

  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "invalid message DocumentIR: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr)
    error->clear();
  return document;
}

} // namespace

std::optional<DocumentIR> lower_message_topic_to_document_ir(
    TopicIdentityIR topic, const MessageTopicIR &message,
    const MessageSectionBlocksIR &blocks, std::string *error) {
  return canonical_document(std::move(topic), message, blocks, error);
}

bool verify_message_topic_document_ir(const MessageTopicIR &message,
                                      const MessageSectionBlocksIR &blocks,
                                      const DocumentIR &document,
                                      std::string *error) {
  const auto expected =
      canonical_document(document.topic, message, blocks, error);
  if (!expected)
    return false;
  if (format_document_ir(*expected) != format_document_ir(document))
    return fail(error, "message DocumentIR differs from canonical lowering");
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
