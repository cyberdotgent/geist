// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/lowering/comment_delivery_document_lowering.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace geist::detail {
namespace {

bool fail(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
  return false;
}

DocumentSourceSliceIR field_slice(const CommentSourceLineIR &line,
                                  const CommentSourceFieldIR &field) {
  DocumentSourceSliceIR slice;
  slice.logical_record = line.logical_record;
  slice.segment_index = line.segment_index;
  slice.token_begin = field.token_begin;
  slice.token_end = field.token_end;
  slice.byte_begin = field.byte_begin;
  slice.byte_end = field.byte_end;
  return slice;
}

DocumentSourceSliceIR fragment_slice(const CommentSourceFragmentIR &fragment,
                                     std::size_t segment_index) {
  DocumentSourceSliceIR slice;
  slice.logical_record = fragment.logical_record;
  slice.segment_index = segment_index;
  slice.token_begin = fragment.token_index;
  slice.token_end = fragment.token_index + 1;
  slice.byte_begin = fragment.byte_begin;
  slice.byte_end = fragment.byte_end;
  return slice;
}

DocumentNodeOriginIR line_origin(const CommentSourceLineIR &line) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = "comment delivery source line";
  for (const auto &field : line.fields)
    if (field.disposition == CommentSourceFieldIR::Disposition::semantic_content) {
      for (const auto &affix : field.affixes)
        if (affix.attachment == CommentAffixAttachment::prefix_current_field)
          origin.slices.push_back(fragment_slice(affix, line.segment_index));
      origin.slices.push_back(field_slice(line, field));
      for (const auto &affix : field.affixes)
        if (affix.attachment == CommentAffixAttachment::suffix_owning_field)
          origin.slices.push_back(fragment_slice(affix, line.segment_index));
    }
  origin.rows.push_back(DocumentSourceRowIR{line.run, line.row});
  return origin;
}

DocumentNodeOriginIR field_origin(const CommentSourceLineIR &line,
                                  const CommentSourceFieldIR &field) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = "comment delivery source field";
  for (const auto &affix : field.affixes)
    if (affix.attachment == CommentAffixAttachment::prefix_current_field)
      origin.slices.push_back(fragment_slice(affix, line.segment_index));
  origin.slices.push_back(field_slice(line, field));
  for (const auto &affix : field.affixes)
    if (affix.attachment == CommentAffixAttachment::suffix_owning_field)
      origin.slices.push_back(fragment_slice(affix, line.segment_index));
  origin.rows.push_back(DocumentSourceRowIR{line.run, line.row});
  return origin;
}

void append_origin(DocumentNodeOriginIR &destination,
                   const DocumentNodeOriginIR &source) {
  destination.slices.insert(destination.slices.end(), source.slices.begin(),
                            source.slices.end());
  for (const auto &row : source.rows)
    if (destination.rows.empty() ||
        destination.rows.back().display_run != row.display_run ||
        destination.rows.back().row_index != row.row_index)
      destination.rows.push_back(row);
}

std::string field_text(const CommentSourceFieldIR &field) {
  auto result = field.text;
  for (const auto &affix : field.affixes) {
    if (affix.attachment == CommentAffixAttachment::prefix_current_field) {
      auto prefix = affix.text;
      if (affix.spacing == CommentAffixSpacing::space_after) prefix += ' ';
      result = prefix + result;
    } else {
      if (affix.spacing == CommentAffixSpacing::space_before) result += ' ';
      result += affix.text;
      if (affix.spacing == CommentAffixSpacing::space_after) result += ' ';
    }
  }
  return result;
}

std::vector<std::size_t> semantic_fields(const CommentSourceLineIR &line) {
  std::vector<std::size_t> result;
  for (std::size_t index = 0; index < line.fields.size(); ++index)
    if (line.fields[index].disposition ==
        CommentSourceFieldIR::Disposition::semantic_content)
      result.push_back(index);
  return result;
}

bool verify_source(const CommentDeliveryIR &delivery, std::string *error) {
  if (delivery.title.empty())
    return fail(error, "comment delivery title is empty");
  const auto expected_blocks =
      delivery.kind == CommentDeliveryKind::delivery_instructions
          ? std::size_t{2}
          : std::size_t{4};
  if (delivery.blocks.size() != expected_blocks)
    return fail(error, "comment delivery block envelope is incomplete");
  if (delivery.blocks[0].kind != CommentDeliveryBlockKind::title_page)
    return fail(error, "comment delivery has no title-page block");
  if (delivery.kind == CommentDeliveryKind::delivery_instructions) {
    if (delivery.blocks[1].kind !=
        CommentDeliveryBlockKind::delivery_instructions)
      return fail(error, "comment delivery instruction block is absent");
  } else if (delivery.blocks[1].kind !=
                 CommentDeliveryBlockKind::questionnaire_table ||
             delivery.blocks[2].kind !=
                 CommentDeliveryBlockKind::questionnaire_table ||
             delivery.blocks[3].kind !=
                 CommentDeliveryBlockKind::response_area) {
    return fail(error, "comment questionnaire block sequence is invalid");
  }

  for (const auto &block : delivery.blocks) {
    if (block.lines.empty())
      return fail(error, "comment delivery block has no source lines");
    if (block.kind == CommentDeliveryBlockKind::questionnaire_table &&
        (block.object_id.empty() || block.object_logical_record == 0 ||
         block.object_token_begin >= block.object_token_end))
      return fail(error, "questionnaire table has no source object provenance");
    for (const auto &line : block.lines) {
      if (line.logical_record == 0 || line.run == 0 ||
          line.token_begin > line.token_end || line.fields.empty())
        return fail(error, "comment source-line provenance is incomplete");
      if (line.marker_disposition == CommentMarkerDisposition::absent) {
        if (line.marker)
          return fail(error, "absent comment marker retains a marker slot");
      } else if (!line.marker) {
        return fail(error, "comment marker disposition has no marker slot");
      }
      if (line.marker_disposition ==
              CommentMarkerDisposition::lexical_content &&
          (line.marker->decoded_text.empty() ||
           line.marker->token_index >= line.token_end))
        return fail(error, "lexical comment marker provenance is invalid");

      auto previous_end = line.token_begin;
      auto semantic_count = std::size_t{0};
      for (const auto &field : line.fields) {
        if (field.token_begin < previous_end ||
            field.token_begin >= field.token_end ||
            field.token_end > line.token_end)
          return fail(error, "comment source fields are out of order");
        previous_end = field.token_end;
        if (field.disposition ==
            CommentSourceFieldIR::Disposition::semantic_content) {
          ++semantic_count;
          if (field.text.empty())
            return fail(error, "semantic comment field is empty");
          for (const auto &affix : field.affixes) {
            if (affix.disposition !=
                    CommentSourceFragmentIR::Disposition::semantic_affix ||
                affix.text.empty() || affix.word_begin >= affix.word_end ||
                affix.byte_begin >= affix.byte_end)
              return fail(error, "semantic comment affix provenance is invalid");
            if ((affix.attachment ==
                     CommentAffixAttachment::prefix_current_field &&
                 affix.spacing == CommentAffixSpacing::space_before) ||
                (affix.attachment ==
                     CommentAffixAttachment::suffix_owning_field &&
                 affix.spacing == CommentAffixSpacing::space_after))
              return fail(error, "semantic comment affix spacing is invalid");
          }
        } else if (!field.affixes.empty()) {
          return fail(error, "layout comment field owns semantic affixes");
        }
      }
      if (semantic_count == 0 &&
          block.kind != CommentDeliveryBlockKind::questionnaire_table)
        return fail(error, "non-table comment line has no semantic content");
      if (line.marker_disposition ==
              CommentMarkerDisposition::lexical_content &&
          semantic_count == 0)
        return fail(error, "lexical comment marker has no semantic field");
    }
  }
  return true;
}

InlineIR text_inline(const CommentSourceLineIR &line,
                     const CommentSourceFieldIR &field) {
  InlineIR result;
  result.node = TextInlineIR{field_text(field)};
  result.origin = field_origin(line, field);
  return result;
}

InlineIR synthesized_separator() {
  InlineIR separator;
  separator.node = TextInlineIR{" "};
  separator.origin.derivation = DocumentDerivationIR::synthesized;
  separator.origin.detail = "semantic field separator";
  return separator;
}

bool continues_paragraph(const CommentSourceLineIR &previous,
                         const CommentSourceLineIR &line) {
  if (previous.run != line.run)
    return false;
  if (line.break_before == PhysicalBreakKind::soft_wrap ||
      line.continues_previous_record ||
      line.marker_disposition == CommentMarkerDisposition::lexical_content)
    return true;

  const auto previous_fields = semantic_fields(previous);

  for (const auto field_index : previous_fields)
    for (const auto &affix : previous.fields[field_index].affixes)
      if (affix.attachment == CommentAffixAttachment::suffix_owning_field &&
          affix.logical_record == line.logical_record &&
          affix.token_index == line.token_begin)
        return true;

  if (previous_fields.empty())
    return false;
  const auto text = field_text(previous.fields[previous_fields.back()]);
  return text.empty() || std::string_view(".!?").find(text.back()) ==
                             std::string_view::npos;
}

bool append_title(const CommentDeliveryBlockIR &source,
                  const std::string &delivery_title, DocumentIR &document,
                  std::string *error) {
  const auto fields = semantic_fields(source.lines.front());
  if (fields.size() != 1 ||
      source.lines.front().fields[fields.front()].text != delivery_title)
    return fail(error, "comment title is not one source-proven semantic field");
  const auto &line = source.lines.front();
  const auto &field = line.fields[fields.front()];
  HeadingBlockIR heading;
  heading.level = 1;
  heading.content.push_back(text_inline(line, field));
  BlockIR block;
  block.node = std::move(heading);
  block.origin = field_origin(line, field);
  block.origin.detail = "comment delivery title heading";
  document.blocks.push_back(std::move(block));
  return true;
}

void append_paragraphs(const CommentDeliveryBlockIR &source,
                       std::size_t first_line, DocumentIR &document) {
  BlockIR current;
  auto has_current = false;
  const CommentSourceLineIR *previous = nullptr;
  const auto flush = [&] {
    if (!has_current)
      return;
    document.blocks.push_back(std::move(current));
    current = BlockIR{};
    has_current = false;
  };
  for (std::size_t line_index = first_line; line_index < source.lines.size();
       ++line_index) {
    const auto &line = source.lines[line_index];
    const auto fields = semantic_fields(line);
    if (fields.empty())
      continue;
    const auto canonical_section_boundary =
        line_index == first_line + std::size_t{2} ||
        (source.kind == CommentDeliveryBlockKind::delivery_instructions &&
         line_index == std::size_t{5});
    if (!has_current || previous == nullptr || canonical_section_boundary ||
        !continues_paragraph(*previous, line)) {
      flush();
      current.node = ParagraphBlockIR{};
      current.origin.derivation = DocumentDerivationIR::semantic_lowering;
      current.origin.detail = "comment delivery paragraph";
      has_current = true;
    }
    auto &paragraph = std::get<ParagraphBlockIR>(current.node);
    for (std::size_t position = 0; position < fields.size(); ++position) {
      const auto &field = line.fields[fields[position]];
      if (!paragraph.content.empty())
        paragraph.content.push_back(synthesized_separator());
      paragraph.content.push_back(text_inline(line, field));
      append_origin(current.origin, field_origin(line, field));
    }
    previous = &line;
  }
  flush();
}

bool append_delivery_instructions(const CommentDeliveryBlockIR &source,
                                  DocumentIR &document, std::string *error) {
  if (source.lines.empty())
    return fail(error, "comment delivery instruction lines are absent");
  const auto final_fields = semantic_fields(source.lines.back());
  if (final_fields.size() != 4)
    return fail(error, "comment delivery checklist geometry is incomplete");

  auto primary = source;
  auto &primary_fields = primary.lines.back().fields;
  primary_fields.erase(primary_fields.begin() +
                           static_cast<std::ptrdiff_t>(final_fields[1]),
                       primary_fields.end());
  append_paragraphs(primary, 0, document);

  const auto &line = source.lines.back();
  const auto &introduction_field = line.fields[final_fields[1]];
  ParagraphBlockIR introduction;
  introduction.content.push_back(text_inline(line, introduction_field));
  BlockIR introduction_block;
  introduction_block.node = std::move(introduction);
  introduction_block.origin = field_origin(line, introduction_field);
  introduction_block.origin.detail = "comment delivery checklist introduction";
  document.blocks.push_back(std::move(introduction_block));

  ListBlockIR checklist;
  BlockIR checklist_block;
  checklist_block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  checklist_block.origin.detail = "comment delivery checklist";
  for (const auto index : {final_fields[2], final_fields[3]}) {
    const auto &field = line.fields[index];
    ListItemIR item;
    item.content.push_back(text_inline(line, field));
    item.origin = field_origin(line, field);
    append_origin(checklist_block.origin, item.origin);
    checklist.items.push_back(std::move(item));
  }
  checklist_block.node = std::move(checklist);
  document.blocks.push_back(std::move(checklist_block));
  return true;
}

void append_preformatted(const CommentDeliveryBlockIR &source,
                         DocumentIR &document) {
  PreformattedBlockIR preformatted;
  BlockIR block;
  block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  block.origin.detail = "comment delivery preformatted response area";
  for (const auto &line : source.lines) {
    const auto fields = semantic_fields(line);
    if (fields.empty())
      continue;
    for (std::size_t position = 0; position < fields.size(); ++position) {
      preformatted.lines.push_back(field_text(line.fields[fields[position]]));
    }
    const auto origin = line_origin(line);
    block.origin.slices.insert(block.origin.slices.end(), origin.slices.begin(),
                               origin.slices.end());
    block.origin.rows.insert(block.origin.rows.end(), origin.rows.begin(),
                             origin.rows.end());
  }
  block.node = std::move(preformatted);
  document.blocks.push_back(std::move(block));
}

struct SemanticFieldRef {
  const CommentSourceLineIR *line = nullptr;
  const CommentSourceFieldIR *field = nullptr;
};

DocumentNodeOriginIR object_origin(const CommentDeliveryBlockIR &source,
                                   std::string detail) {
  auto origin = DocumentNodeOriginIR{};
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = std::move(detail);
  origin.slices.push_back(DocumentSourceSliceIR{
      source.object_logical_record, source.object_segment_index,
      source.object_token_begin, source.object_token_end,
      source.object_byte_begin, source.object_byte_end});
  return origin;
}

bool append_table_anchors(const CommentDeliveryBlockIR &source,
                          DocumentIR &document, std::string *error) {
  constexpr std::string_view prefix = "SRTBL";
  if (source.object_id.size() <= prefix.size() ||
      source.object_id.compare(0, prefix.size(), prefix) != 0)
    return fail(error, "questionnaire table object id is not canonical");
  const auto id = source.object_id.substr(prefix.size());
  // Two spellings of one destination.  `TBL`+id is the canonical table
  // anchor, so it is the one the link map resolves references against; the
  // bare id is a second name for the same place and must not add a second
  // entry, or a book with both spellings would resolve one of them twice.
  const std::pair<std::string, AnchorRoleIR> spellings[] = {
      {id, AnchorRoleIR::local},
      {"TBL" + id, AnchorRoleIR::table},
  };
  for (const auto &spelling : spellings) {
    BlockIR anchor;
    anchor.node = AnchorBlockIR{spelling.first, spelling.second};
    anchor.origin = object_origin(source, "comment questionnaire anchor");
    document.blocks.push_back(std::move(anchor));
  }
  return true;
}

bool append_table(const CommentDeliveryBlockIR &source, DocumentIR &document,
                  std::string *error) {
  if (!append_table_anchors(source, document, error))
    return false;
  std::vector<SemanticFieldRef> fields;
  for (const auto &line : source.lines) {
    const auto indices = semantic_fields(line);
    for (std::size_t position = 0; position < indices.size(); ++position)
      fields.push_back(
          SemanticFieldRef{&line, &line.fields[indices[position]]});
  }
  if (fields.size() != 6 && fields.size() != 22)
    return fail(error,
                "questionnaire table does not have bounded field geometry");

  // The field geometry above proves the form's shape, but the form is drawn,
  // not tabulated: hosted BookServer serves SC31-711 COMMENTS `TBLUNIQ8` and
  // `TBLUNIQ9` inside the topic's `<pre width="80">` as the underscore-and-bar
  // box itself -- `   | <B>Overall,</B> ... |   Satisfied   |  Dissatisfied |`
  // over `   |____...____|_______________|_______________|` -- and emits no
  // `<table>` element on the page (DT 19941010174546).  This family's source
  // lines are one visible field each and carry no box rule, so the verbatim
  // text is rebuilt from the proven three-field rows at a single column stop:
  // the same reading order and the same fixed columns, without asserting a
  // grid the reader never shows.
  std::vector<std::vector<const SemanticFieldRef *>> rows;
  const auto append_row = [&](std::size_t begin, std::size_t count) {
    std::vector<const SemanticFieldRef *> row;
    for (std::size_t position = begin; position < begin + count; ++position)
      row.push_back(&fields[position]);
    rows.push_back(std::move(row));
  };
  append_row(0, 3);
  auto offset = std::size_t{3};
  if (fields.size() == 22) {
    append_row(offset, 1);
    ++offset;
  }
  while (offset < fields.size()) {
    append_row(offset, 3);
    offset += 3;
  }
  std::size_t stop = 0;
  for (const auto &row : rows)
    if (!row.empty())
      stop = std::max(stop, field_text(*row.front()->field).size());
  stop += 2;
  PreformattedBlockIR drawn;
  for (const auto &row : rows) {
    std::string text;
    for (std::size_t index = 0; index < row.size(); ++index) {
      if (index != 0 && text.size() < stop * index)
        text.append(stop * index - text.size(), ' ');
      text += field_text(*row[index]->field);
    }
    while (!text.empty() && text.back() == ' ')
      text.pop_back();
    drawn.lines.push_back(std::move(text));
  }

  BlockIR block;
  block.node = std::move(drawn);
  block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  block.origin.detail = "comment questionnaire form: verbatim rows";
  for (const auto &line : source.lines) {
    const auto fields = semantic_fields(line);
    if (fields.empty())
      continue;
    const auto origin = line_origin(line);
    block.origin.slices.insert(block.origin.slices.end(), origin.slices.begin(),
                               origin.slices.end());
    block.origin.rows.insert(block.origin.rows.end(), origin.rows.begin(),
                             origin.rows.end());
  }
  document.blocks.push_back(std::move(block));
  return true;
}

} // namespace

std::optional<DocumentIR>
lower_comment_delivery_to_document_ir(TopicIdentityIR topic,
                                      const CommentDeliveryIR &delivery,
                                      std::string *error) {
  if (!verify_source(delivery, error))
    return std::nullopt;
  DocumentIR document;
  document.topic = std::move(topic);
  if (!append_title(delivery.blocks.front(), delivery.title, document, error))
    return std::nullopt;
  append_paragraphs(delivery.blocks.front(), 1, document);
  if (delivery.kind == CommentDeliveryKind::delivery_instructions) {
    if (!append_delivery_instructions(delivery.blocks[1], document, error))
      return std::nullopt;
  } else {
    if (!append_table(delivery.blocks[1], document, error) ||
        !append_table(delivery.blocks[2], document, error))
      return std::nullopt;
    append_preformatted(delivery.blocks[3], document);
  }
  // Every container names at least the BOO bytes its own content names
  // before the document is verified.
  normalize_document_origin_slices(document);
  std::string document_error;
  if (!verify_document_ir(document, &document_error)) {
    fail(error, "lowered comment document is invalid: " + document_error);
    return std::nullopt;
  }
  if (error != nullptr)
    error->clear();
  return document;
}

bool verify_comment_delivery_document_ir(const CommentDeliveryIR &delivery,
                                         const DocumentIR &document,
                                         std::string *error) {
  std::string document_error;
  if (!verify_document_ir(document, &document_error))
    return fail(error, "comment document is invalid: " + document_error);
  const auto canonical = lower_comment_delivery_to_document_ir(
      document.topic, delivery, &document_error);
  if (!canonical)
    return fail(error, "comment source cannot be lowered: " + document_error);
  if (format_document_ir(*canonical) != format_document_ir(document))
    return fail(error, "comment document differs from canonical lowering");
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
