#include "geist/detail/message_document_lowering.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

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
  return !paragraph.text.empty() &&
         (!paragraph.source_rows.empty() || !paragraph.source_segments.empty());
}

bool paragraph_coordinates_exist(const MessageTopicIR &message,
                                 const MessageParagraphIR &paragraph) {
  return std::all_of(paragraph.source_segments.begin(),
                     paragraph.source_segments.end(),
                     [&](const auto &coordinate) {
                       return find_segment(message, coordinate.first,
                                           coordinate.second) != nullptr;
                     });
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
      if (section.paragraphs.empty())
        return fail(error, "message section has no paragraphs");
      for (const auto &paragraph : section.paragraphs)
        if (!source_proven(paragraph) ||
            !paragraph_coordinates_exist(message, paragraph))
          return fail(error,
                      "message section paragraph lacks source provenance");
    }
  }
  return true;
}

BlockIR paragraph_block(InlineSequenceIR content,
                        DocumentNodeOriginIR block_origin) {
  return {ParagraphBlockIR{std::move(content)}, std::move(block_origin)};
}

std::optional<DocumentIR> canonical_document(TopicIdentityIR topic,
                                             const MessageTopicIR &message,
                                             std::string *error) {
  if (!verify_message_shape(message, error))
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
    document.blocks.push_back(paragraph_block(
        {{EmphasisInlineIR{std::move(headline_text)}, headline_inline_origin}},
        std::move(headline_origin)));

    for (const auto &section : entry.sections) {
      for (std::size_t paragraph_index = 0;
           paragraph_index < section.paragraphs.size(); ++paragraph_index) {
        const auto &paragraph = section.paragraphs[paragraph_index];
        auto text_origin = paragraph_origin(message, paragraph,
                                            "message section paragraph text");
        InlineSequenceIR content;
        auto block_origin = text_origin;
        if (paragraph_index == 0) {
          auto label_origin = origin("message section label");
          for (const auto &row : section.label_source_rows)
            add_row(label_origin, {row.first, row.second});
          const auto *label_segment = find_segment(
              message, section.logical_record, section.segment_index);
          if (label_segment != nullptr)
            add_slice(label_origin, label_segment->source);
          canonicalize(label_origin);
          merge_origin(block_origin, label_origin);
          const auto *label = section.kind == MessageSectionKind::meaning
                                  ? "Meaning:"
                                  : "Action:";
          content.push_back({EmphasisInlineIR{label}, std::move(label_origin)});
          auto separator_origin = origin("message section separator");
          separator_origin.derivation = DocumentDerivationIR::synthesized;
          content.push_back({TextInlineIR{" "}, std::move(separator_origin)});
        }
        content.push_back(
            {TextInlineIR{paragraph.text}, std::move(text_origin)});
        document.blocks.push_back(
            paragraph_block(std::move(content), std::move(block_origin)));
      }
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
    TopicIdentityIR topic, const MessageTopicIR &message, std::string *error) {
  return canonical_document(std::move(topic), message, error);
}

bool verify_message_topic_document_ir(const MessageTopicIR &message,
                                      const DocumentIR &document,
                                      std::string *error) {
  const auto expected = canonical_document(document.topic, message, error);
  if (!expected)
    return false;
  if (format_document_ir(*expected) != format_document_ir(document))
    return fail(error, "message DocumentIR differs from canonical lowering");
  if (error != nullptr)
    error->clear();
  return true;
}

} // namespace geist::detail
