#include "geist/detail/comment_delivery_document_lowering.hpp"

#include <algorithm>
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
  return slice;
}

DocumentSourceSliceIR marker_slice(const MarkerSlotIR &marker,
                                   std::size_t segment_index) {
  DocumentSourceSliceIR slice;
  slice.logical_record = marker.logical_record;
  slice.segment_index = segment_index;
  slice.token_begin = marker.token_index;
  slice.token_end = marker.token_index + 1;
  slice.byte_begin = marker.byte_range.begin;
  slice.byte_end = marker.byte_range.end;
  return slice;
}

DocumentNodeOriginIR line_origin(const CommentSourceLineIR &line,
                                 bool include_marker = true) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = "comment delivery source line";
  if (include_marker &&
      line.marker_disposition == CommentMarkerDisposition::lexical_content)
    origin.slices.push_back(marker_slice(*line.marker, line.segment_index));
  for (const auto &field : line.fields)
    if (field.disposition ==
        CommentSourceFieldIR::Disposition::semantic_content)
      origin.slices.push_back(field_slice(line, field));
  origin.rows.push_back(DocumentSourceRowIR{line.run, line.row});
  return origin;
}

DocumentNodeOriginIR field_origin(const CommentSourceLineIR &line,
                                  const CommentSourceFieldIR &field,
                                  bool prepend_marker) {
  DocumentNodeOriginIR origin;
  origin.derivation = DocumentDerivationIR::semantic_lowering;
  origin.detail = "comment delivery source field";
  if (prepend_marker)
    origin.slices.push_back(marker_slice(*line.marker, line.segment_index));
  origin.slices.push_back(field_slice(line, field));
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

std::string field_text(const CommentSourceLineIR &line,
                       const CommentSourceFieldIR &field, bool prepend_marker) {
  if (!prepend_marker)
    return field.text;
  return line.marker->decoded_text + " " + field.text;
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
        block.object_id.empty())
      return fail(error, "questionnaire table has no source object id");
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
                     const CommentSourceFieldIR &field, bool prepend_marker) {
  InlineIR result;
  result.node = TextInlineIR{field_text(line, field, prepend_marker)};
  result.origin = field_origin(line, field, prepend_marker);
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
  return line.break_before == PhysicalBreakKind::soft_wrap ||
         line.continues_previous_record ||
         line.marker_disposition == CommentMarkerDisposition::lexical_content;
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
  heading.content.push_back(text_inline(line, field, false));
  BlockIR block;
  block.node = std::move(heading);
  block.origin = field_origin(line, field, false);
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
    if (!has_current || previous == nullptr ||
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
      const auto prepend =
          position == 0 &&
          line.marker_disposition == CommentMarkerDisposition::lexical_content;
      if (!paragraph.content.empty())
        paragraph.content.push_back(synthesized_separator());
      paragraph.content.push_back(text_inline(line, field, prepend));
      append_origin(current.origin, field_origin(line, field, prepend));
    }
    previous = &line;
  }
  flush();
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
      const auto prepend =
          position == 0 &&
          line.marker_disposition == CommentMarkerDisposition::lexical_content;
      preformatted.lines.push_back(
          field_text(line, line.fields[fields[position]], prepend));
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
  bool prepend_marker = false;
};

bool append_table(const CommentDeliveryBlockIR &source, DocumentIR &document,
                  std::string *error) {
  std::vector<SemanticFieldRef> fields;
  for (const auto &line : source.lines) {
    const auto indices = semantic_fields(line);
    for (std::size_t position = 0; position < indices.size(); ++position)
      fields.push_back(SemanticFieldRef{
          &line, &line.fields[indices[position]],
          position == 0 && line.marker_disposition ==
                               CommentMarkerDisposition::lexical_content});
  }
  if (fields.size() != 6 && fields.size() != 22)
    return fail(error,
                "questionnaire table does not have bounded field geometry");

  TableBlockIR table;
  const auto append_row = [&](std::size_t begin, std::size_t count) {
    TableRowIR row;
    row.origin.derivation = DocumentDerivationIR::semantic_lowering;
    row.origin.detail = "comment questionnaire row";
    for (std::size_t position = begin; position < begin + count; ++position) {
      const auto &field = fields[position];
      TableCellIR cell;
      cell.content.push_back(
          text_inline(*field.line, *field.field, field.prepend_marker));
      cell.origin =
          field_origin(*field.line, *field.field, field.prepend_marker);
      cell.origin.detail = "comment questionnaire cell";
      append_origin(row.origin, cell.origin);
      row.cells.push_back(std::move(cell));
    }
    while (row.cells.size() < 3) {
      TableCellIR padding;
      padding.origin.derivation = DocumentDerivationIR::synthesized;
      padding.origin.detail = "rectangular questionnaire table padding";
      row.cells.push_back(std::move(padding));
    }
    table.rows.push_back(std::move(row));
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
  table.header_rows = 1;

  BlockIR block;
  block.node = std::move(table);
  block.origin.derivation = DocumentDerivationIR::semantic_lowering;
  block.origin.detail = "comment questionnaire table";
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
    append_paragraphs(delivery.blocks[1], 0, document);
  } else {
    if (!append_table(delivery.blocks[1], document, error) ||
        !append_table(delivery.blocks[2], document, error))
      return std::nullopt;
    append_preformatted(delivery.blocks[3], document);
  }
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
