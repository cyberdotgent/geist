#include "geist/detail/topic_document_lowering.hpp"

#include "geist/detail/comment_delivery_document_lowering.hpp"
#include "geist/detail/comment_delivery_ir.hpp"
#include "geist/detail/fixed_prose_document_lowering.hpp"
#include "geist/detail/fixed_prose_topic_ir.hpp"
#include "geist/detail/generated_list_document_lowering.hpp"
#include "geist/detail/generated_list_topic_ir.hpp"
#include "geist/detail/glossary_catalog_ir.hpp"
#include "geist/detail/glossary_document_lowering.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/menu_document_lowering.hpp"
#include "geist/detail/menu_ir.hpp"
#include "geist/detail/menu_topic_ir.hpp"
#include "geist/detail/message_document_lowering.hpp"
#include "geist/detail/message_topic_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "geist/detail/prose_topic_document_lowering.hpp"
#include "geist/detail/prose_topic_ir.hpp"
#include "geist/detail/publication_document_lowering.hpp"
#include "geist/detail/publication_ir.hpp"
#include "geist/detail/selector_ir.hpp"
#include "geist/detail/trap_catalog_document_lowering.hpp"
#include "geist/detail/trap_catalog_ir.hpp"

#include <algorithm>
#include <utility>

namespace geist::detail {
namespace {

void reject(std::string *error, std::string message) {
  if (error != nullptr)
    *error = std::move(message);
}

void prepend_topic_id_to_heading(DocumentIR &document) {
  if (document.topic.id.empty() || document.blocks.empty())
    return;
  const auto heading_block = std::find_if(
      document.blocks.begin(), document.blocks.end(), [](auto &block) {
        return std::holds_alternative<HeadingBlockIR>(block.node);
      });
  if (heading_block == document.blocks.end())
    return;
  auto &heading = std::get<HeadingBlockIR>(heading_block->node);

  InlineIR identity;
  identity.node = TextInlineIR{document.topic.id + " "};
  identity.origin.derivation = DocumentDerivationIR::synthesized;
  identity.origin.detail = "public topic identity prefix";
  heading.content.insert(heading.content.begin(), std::move(identity));
}

bool generated_list_source_candidate(
    const std::vector<DecodedLogicalRecordSource> &sources) {
  bool heading = false;
  bool title = false;
  bool selector = false;
  for (const auto &record : sources) {
    const auto text = token_words_to_ascii(record.assembled.words);
    for (const auto &segment : record.control_segments) {
      if (segment.kind == BookControlKind::heading_level) {
        const auto operand = ascii_lower(trim_ascii(text.substr(
            segment.operand_range.begin,
            segment.operand_range.end - segment.operand_range.begin)));
        heading = heading || operand == ":figlist" || operand == ":tlist";
      } else if (segment.kind == BookControlKind::title) {
        title = true;
      } else if (segment.kind == BookControlKind::select) {
        selector = true;
      }
    }
  }
  return heading && title && selector;
}

bool message_source_candidate(
    const std::vector<DecodedLogicalRecordSource> &sources) {
  auto message_starts = std::size_t{};
  for (const auto &record : sources)
    message_starts += static_cast<std::size_t>(
        std::count_if(record.control_segments.begin(),
                      record.control_segments.end(), [](const auto &segment) {
                        return segment.kind == BookControlKind::message_start;
                      }));
  return message_starts > 1;
}

} // namespace

std::optional<DocumentIR> try_lower_topic_to_document_ir(
    TopicIdentityIR topic,
    const std::vector<DecodedLogicalRecordSource> &sources,
    const BookTopicCatalogIR *book_topic_catalog,
    std::string *typed_rejection, TypedLoweringTraceIR *trace) {
  if (typed_rejection != nullptr)
    typed_rejection->clear();
  if (trace != nullptr)
    *trace = TypedLoweringTraceIR{};
  if (sources.empty())
    return std::nullopt;
  // Recognizer diagnostics are collected only when a trace is requested; the
  // production route passes no error sinks, exactly as before.
  std::string declined;
  std::string *const declined_sink = trace != nullptr ? &declined : nullptr;
  const auto note_declined = [&](const char *family, bool matched) {
    if (trace == nullptr || matched) {
      declined.clear();
      return;
    }
    trace->declined.push_back(std::string(family) + ": " +
                              (declined.empty() ? "declined" : declined));
    declined.clear();
  };

  const auto layout = extract_layout_ir(sources);
  std::string error;
  if (!verify_layout_ir(sources, layout, &error)) {
    reject(typed_rejection, "topic layout rejected: " + error);
    return std::nullopt;
  }
  const auto ownership = build_ownership_ir(sources, layout);
  if (!verify_ownership_ir(sources, layout, ownership, &error)) {
    reject(typed_rejection, "topic ownership rejected: " + error);
    return std::nullopt;
  }

  const auto delivery =
      extract_comment_delivery_ir(sources, layout, ownership, declined_sink);
  note_declined("comment delivery", delivery.has_value());
  const auto publications =
      extract_publication_catalog_ir(sources, layout, ownership);
  note_declined("publication catalog", publications.has_value());
  const auto fixed_prose =
      extract_fixed_prose_topic_ir(sources, layout, ownership, declined_sink);
  note_declined("fixed prose", fixed_prose.has_value());
  const auto glossary =
      extract_glossary_catalog_ir(sources, layout, ownership, declined_sink);
  note_declined("glossary", glossary.has_value());
  std::optional<MessageTopicIR> message;
  std::optional<TrapCatalogIR> trap_catalog;
  if (message_source_candidate(sources)) {
    message =
        extract_message_topic_ir(sources, layout, ownership, declined_sink);
    note_declined("message catalog", message.has_value());
    // Both SRMSG families share the source envelope; the Meaning/Action
    // catalog is the more specific recognizer, so the section-label catalog
    // is offered only where it declines.
    if (!message) {
      trap_catalog = extract_trap_catalog_ir(sources, layout, ownership,
                                             topic.title, declined_sink);
      note_declined("trap catalog", trap_catalog.has_value());
    }
  }
  std::optional<GeneratedListTopicIR> generated_list;
  std::optional<SelectorCatalogIR> generated_selectors;
  if (generated_list_source_candidate(sources)) {
    generated_selectors = extract_selector_catalog_ir(sources, declined_sink);
    note_declined("generated list selectors", generated_selectors.has_value());
    if (generated_selectors) {
      generated_list = extract_generated_list_topic_ir(
          sources, *generated_selectors, layout, ownership, declined_sink);
      note_declined("generated list", generated_list.has_value());
    }
  }
  std::optional<MenuTopicIR> menu;
  std::optional<MenuTargetValidationIR> menu_validation;
  if (book_topic_catalog != nullptr) {
    const auto raw_menu = extract_source_menu_ir(sources, declined_sink);
    note_declined("menu source", raw_menu.has_value());
    if (raw_menu) {
      menu_validation = validate_source_menu_targets(
          *raw_menu, *book_topic_catalog, declined_sink);
      note_declined("menu targets", menu_validation.has_value());
      if (menu_validation) {
        menu = extract_menu_topic_ir(sources, *menu_validation, layout,
                                     ownership, declined_sink);
        note_declined("menu", menu.has_value());
      }
    }
  }
  const auto family_count = static_cast<unsigned>(delivery.has_value()) +
                            static_cast<unsigned>(publications.has_value()) +
                            static_cast<unsigned>(fixed_prose.has_value()) +
                            static_cast<unsigned>(glossary.has_value()) +
                            static_cast<unsigned>(message.has_value()) +
                            static_cast<unsigned>(trap_catalog.has_value()) +
                            static_cast<unsigned>(generated_list.has_value()) +
                            static_cast<unsigned>(menu.has_value());
  if (family_count == 0) {
    // Ordinary prose is offered last: only a topic every specific family
    // declined can be admitted here, so the family count stays at one.
    auto prose = extract_prose_topic_ir(sources, layout, ownership, topic.title,
                                        book_topic_catalog, &error);
    if (!prose) {
      reject(typed_rejection, "prose topic rejected: " + error);
      return std::nullopt;
    }
    if (!verify_prose_topic_ir(sources, layout, ownership, topic.title,
                               book_topic_catalog, *prose, &error)) {
      reject(typed_rejection, "prose semantics rejected: " + error);
      return std::nullopt;
    }
    auto document = lower_prose_topic_to_document_ir(topic, *prose, &error);
    if (!document ||
        !verify_prose_topic_document_ir(*prose, *document, &error)) {
      reject(typed_rejection, "prose document rejected: " + error);
      return std::nullopt;
    }
    prepend_topic_id_to_heading(*document);
    if (!verify_document_ir(*document, &error)) {
      reject(typed_rejection, "prose identity policy rejected: " + error);
      return std::nullopt;
    }
    return document;
  }
  if (family_count != 1) {
    reject(typed_rejection,
           "topic source ambiguously matches multiple typed families");
    return std::nullopt;
  }

  std::optional<DocumentIR> document;
  std::string family;
  // `family` is assigned once below; publish it to the trace on every exit so
  // a verification rejection still names the family that claimed the topic.
  struct FamilyPublisher {
    const std::string &family;
    TypedLoweringTraceIR *trace;
    ~FamilyPublisher() {
      if (trace != nullptr)
        trace->family = family;
    }
  } family_publisher{family, trace};
  if (delivery) {
    family = "comment delivery";
    if (!verify_comment_delivery_ir(sources, layout, ownership, *delivery,
                                    &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    document = lower_comment_delivery_to_document_ir(topic, *delivery, &error);
    if (!document ||
        !verify_comment_delivery_document_ir(*delivery, *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  } else if (publications) {
    family = "publication catalog";
    if (!verify_publication_catalog_ir(sources, layout, ownership,
                                       *publications, &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    // The verified source model is authoritative. TopicData's compatibility
    // metadata field may include packed controls after CHDLEVEL when the
    // source uses spaced separators.
    topic.heading_level = publications->heading_level;
    document =
        lower_publication_catalog_to_document_ir(topic, *publications, &error);
    if (!document || !verify_publication_catalog_document_ir(
                         *publications, *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  } else if (fixed_prose) {
    family = "fixed prose";
    if (!verify_fixed_prose_topic_ir(sources, layout, ownership, *fixed_prose,
                                     &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    document =
        lower_fixed_prose_topic_to_document_ir(topic, *fixed_prose, &error);
    if (!document || !verify_fixed_prose_topic_document_ir(*fixed_prose,
                                                           *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  } else if (glossary) {
    family = "glossary";
    if (!verify_glossary_catalog_ir(sources, layout, ownership, *glossary,
                                    &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    topic.heading_level = glossary->heading_level;
    document = lower_glossary_catalog_to_document_ir(topic, *glossary, &error);
    if (!document ||
        !verify_glossary_catalog_document_ir(*glossary, *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  } else if (message) {
    family = "message catalog";
    if (!verify_message_topic_ir(sources, layout, ownership, *message,
                                 &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    topic.heading_level = message->metadata.heading_level;
    const auto blocks = extract_message_section_blocks_ir(layout, ownership,
                                                          message->catalog);
    if (!verify_message_section_blocks_ir(layout, ownership, message->catalog,
                                          blocks, &error)) {
      reject(typed_rejection, family + " structured blocks rejected: " + error);
      return std::nullopt;
    }
    document =
        lower_message_topic_to_document_ir(topic, *message, blocks, &error);
    if (!document || !verify_message_topic_document_ir(*message, blocks,
                                                       *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  } else if (trap_catalog) {
    family = "trap catalog";
    if (!verify_trap_catalog_ir(sources, layout, ownership, *trap_catalog,
                                &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    topic.heading_level = trap_catalog->heading_level;
    document =
        lower_trap_catalog_to_document_ir(topic, *trap_catalog, &error);
    if (!document ||
        !verify_trap_catalog_document_ir(*trap_catalog, *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  } else if (generated_list) {
    family = "generated list";
    if (!generated_selectors ||
        !verify_generated_list_topic_ir(sources, *generated_selectors, layout,
                                        ownership, *generated_list, &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    document = lower_generated_list_topic_to_document_ir(topic, *generated_list,
                                                         &error);
    if (!document || !verify_generated_list_topic_document_ir(
                         *generated_list, *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  } else {
    family = "menu";
    if (!verify_menu_topic_ir(sources, *menu_validation, layout, ownership,
                              *menu, &error)) {
      reject(typed_rejection, family + " semantics rejected: " + error);
      return std::nullopt;
    }
    document = lower_menu_topic_to_document_ir(topic, *menu, &error);
    if (!document || !verify_menu_topic_document_ir(*menu, *document, &error)) {
      reject(typed_rejection, family + " document rejected: " + error);
      return std::nullopt;
    }
  }

  prepend_topic_id_to_heading(*document);
  if (!verify_document_ir(*document, &error)) {
    reject(typed_rejection, family + " identity policy rejected: " + error);
    return std::nullopt;
  }
  return document;
}

} // namespace geist::detail
