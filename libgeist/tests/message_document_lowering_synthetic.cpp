#include "geist/boo.hpp"
#include "test_failures.hpp"
#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/message_document_lowering.hpp"
#include "geist/detail/message_section_blocks_ir.hpp"
#include "geist/detail/message_topic_ir.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << '\n';
    geist_test::record_failure();
    return;
  }
}

void open_context(const std::filesystem::path &path,
                  geist::detail::LogicalDecodeContext &context) {
  context.bytes = geist::detail::read_file(path);
  const auto directory_page = geist::detail::read_be16(context.bytes, 0);
  const auto base =
      static_cast<std::size_t>(directory_page) * geist::boo_page_size;
  context.directory.page_number = directory_page;
  context.directory.token_threshold = context.bytes[base + 0x14];
  context.directory.token_map_offset =
      geist::detail::read_be16(context.bytes, base + 0x22);
  context.directory.dictionary_start_page =
      geist::detail::read_be16(context.bytes, base + 0x28);
  context.directory.dictionary_page_count =
      geist::detail::read_be16(context.bytes, base + 0x2e);
  context.directory.logical_record_count =
      geist::detail::read_be16(context.bytes, base + 0x36);
  context.directory.content_page_count =
      geist::detail::read_be16(context.bytes, base + 0x38);
  context.directory.content_start_page =
      geist::detail::read_be16(context.bytes, base + 0x3a);
  context.decoded_records = geist::detail::decode_experimental_logical_records(
      context.bytes, context.directory, &context.record_payload_ranges);
}

std::size_t count_blocks(const geist::detail::DocumentIR &document,
                         bool (*predicate)(const geist::detail::BlockIR &)) {
  return static_cast<std::size_t>(
      std::count_if(document.blocks.begin(), document.blocks.end(), predicate));
}

bool anchor_block(const geist::detail::BlockIR &block) {
  return std::holds_alternative<geist::detail::AnchorBlockIR>(block.node);
}

bool paragraph_block(const geist::detail::BlockIR &block) {
  return std::holds_alternative<geist::detail::ParagraphBlockIR>(block.node);
}

bool table_block(const geist::detail::BlockIR &block) {
  return std::holds_alternative<geist::detail::TableBlockIR>(block.node);
}

bool list_block(const geist::detail::BlockIR &block) {
  return std::holds_alternative<geist::detail::ListBlockIR>(block.node);
}

bool preformatted_block(const geist::detail::BlockIR &block) {
  return std::holds_alternative<geist::detail::PreformattedBlockIR>(block.node);
}

// The first block of the requested kind between the named anchor and the
// next anchor.
const geist::detail::BlockIR *
find_block_after(const geist::detail::DocumentIR &document,
                 const std::string &anchor,
                 bool (*predicate)(const geist::detail::BlockIR &)) {
  bool inside = false;
  for (const auto &block : document.blocks) {
    if (const auto *node =
            std::get_if<geist::detail::AnchorBlockIR>(&block.node)) {
      if (inside)
        return nullptr;
      inside = node->id == anchor;
      continue;
    }
    if (inside && predicate(block))
      return &block;
  }
  return nullptr;
}

std::string inline_text(const geist::detail::InlineSequenceIR &content) {
  std::string result;
  for (const auto &in : content)
    if (const auto *text = std::get_if<geist::detail::TextInlineIR>(&in.node))
      result += text->text;
  return result;
}

std::size_t substring_count(const std::string &text,
                            const std::string &needle) {
  std::size_t result = 0;
  for (auto at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size()))
    ++result;
  return result;
}

} // namespace

int main() {
  const auto path =
      std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / "SC31-711.boo";
  geist::detail::LogicalDecodeContext context;
  open_context(path, context);
  const auto sources =
      geist::detail::decode_logical_record_sources(context, 172, 435);
  const auto layout = geist::detail::extract_layout_ir(sources);
  std::string error;
  const auto verified =
      geist::detail::build_verified_ownership_ir(sources, layout, &error);
  require(verified.has_value(),
          error.empty() ? "message ownership is not verifiable" : error);
  const auto& ownership = verified->ir();
  const auto message = geist::detail::extract_message_topic_ir(
      sources, layout, *verified, &error);
  require(message.has_value(),
          error.empty() ? "message extraction failed" : error);
  const auto blocks = geist::detail::extract_message_section_blocks_ir(
      layout, ownership, message->catalog);
  require(geist::detail::verify_message_section_blocks_ir(
              layout, ownership, message->catalog, blocks, &error),
          error.empty() ? "structured block verification failed" : error);

  geist::detail::TopicIdentityIR identity;
  identity.id = "5.0";
  identity.title = "stale compatibility title";
  identity.heading_level = "h6";
  identity.topic_number = 71;
  identity.start_logical_record = 172;
  identity.end_logical_record = 435;
  const auto document = geist::detail::lower_message_topic_to_document_ir(
      identity, *message, blocks, &error);
  require(document.has_value(),
          error.empty() ? "message lowering failed" : error);
  require(geist::detail::verify_document_ir(*document, &error), error);
  require(geist::detail::verify_message_topic_document_ir(
              *message, blocks, *document, &error),
          error.empty() ? "message DocumentIR verification failed" : error);
  require(document->topic.id == "5.0" &&
              document->topic.title == "Chapter 5. Messages" &&
              document->topic.heading_level == "H1" &&
              document->topic.start_logical_record == 172 &&
              document->topic.end_logical_record == 435,
          "message source metadata did not become authoritative identity");

  require(
      document->blocks.size() > 8 &&
          std::get<geist::detail::AnchorBlockIR>(document->blocks[0].node).id ==
              "MSG" &&
          std::get<geist::detail::AnchorBlockIR>(document->blocks[1].node).id ==
              "HDRMSGS" &&
          std::holds_alternative<geist::detail::HeadingBlockIR>(
              document->blocks[2].node),
      "message header blocks are not in source order");
  for (std::size_t paragraph = 0; paragraph < 5; ++paragraph)
    require(std::holds_alternative<geist::detail::ParagraphBlockIR>(
                document->blocks[3 + paragraph].node),
            "message introduction did not remain five paragraphs");
  require(std::get<geist::detail::AnchorBlockIR>(document->blocks[8].node).id ==
              "MSG 023",
          "first numeric message does not follow the introduction");
  require(count_blocks(*document, anchor_block) == 398,
          "message DocumentIR lost source anchors");

  std::size_t expected_paragraphs = message->introduction.paragraphs.size();
  for (const auto &entry : message->catalog.entries) {
    expected_paragraphs += 1;
    expected_paragraphs += entry.sections.size();
  }
  // MSG739's checklist is followed by prose on the same source row as its
  // last item; that remainder is its own paragraph after the list.
  require(count_blocks(*document, paragraph_block) == expected_paragraphs + 1,
          "message DocumentIR lost a semantic section paragraph");
  require(count_blocks(*document, table_block) == 1 &&
              count_blocks(*document, list_block) == 1 &&
              count_blocks(*document, preformatted_block) == 1,
          "message DocumentIR did not lower exactly the three verified blocks");

  const auto *table = find_block_after(*document, "MSG 807", table_block);
  require(table != nullptr, "MSG807 table does not follow its anchor");
  if (table != nullptr) {
    const auto &node = std::get<geist::detail::TableBlockIR>(table->node);
    require(node.header_rows == 1 && node.rows.size() == 26 &&
                node.rows.front().cells.size() == 2 &&
                inline_text(node.rows[0].cells[0].content) == "Command type" &&
                inline_text(node.rows[0].cells[1].content) == "Command" &&
                inline_text(node.rows[1].cells[0].content) == "23006" &&
                inline_text(node.rows[25].cells[0].content) == "103000" &&
                inline_text(node.rows[25].cells[1].content) ==
                    "LAN CAUQUAL LIST",
            "MSG807 table schema or cells changed");
    for (const auto &row : node.rows)
      for (const auto &cell : row.cells)
        require(!cell.origin.rows.empty() && !cell.origin.slices.empty() &&
                    cell.origin.derivation ==
                        geist::detail::DocumentDerivationIR::semantic_lowering,
                "MSG807 table cell lacks row/token provenance");
    // The block sits directly between its Meaning paragraph and the Action
    // paragraph; no prose is displaced around it.
    const auto index =
        static_cast<std::size_t>(table - document->blocks.data());
    const auto *meaning = std::get_if<geist::detail::ParagraphBlockIR>(
        &document->blocks[index - 1].node);
    const auto *action = std::get_if<geist::detail::ParagraphBlockIR>(
        &document->blocks[index + 1].node);
    require(meaning != nullptr && action != nullptr &&
                std::get<geist::detail::EmphasisInlineIR>(
                    meaning->content[0].node)
                        .text == "Meaning:" &&
                std::get<geist::detail::EmphasisInlineIR>(
                    meaning->content[0].node)
                        .kind == geist::detail::EmphasisKindIR::strong &&
                std::get<geist::detail::EmphasisInlineIR>(
                    action->content[0].node)
                        .text == "Action:",
            "MSG807 table is not enclosed by its Meaning and Action blocks");
  }
  const auto *list = find_block_after(*document, "MSG 739", list_block);
  require(list != nullptr, "MSG739 list does not follow its anchor");
  if (list != nullptr) {
    const auto &node = std::get<geist::detail::ListBlockIR>(list->node);
    const auto index =
        static_cast<std::size_t>(list - document->blocks.data());
    const auto *after = std::get_if<geist::detail::ParagraphBlockIR>(
        &document->blocks[index + 1].node);
    require(!node.ordered && node.items.size() == 3 &&
                inline_text(node.items[0].content) ==
                    "/usr/lpp/lnm/databases contains lnmlnmemgr.pdf" &&
                inline_text(node.items[2].content) ==
                    "/usr/lib/nls/msg/<lang> contains a symbolic link to "
                    "/usr/lpp/lnm/nls/<lang>/lnmlnmemgr_dfi.cat" &&
                after != nullptr && after->content.size() == 1 &&
                inline_text(after->content) ==
                    "If everything is correctly set, contact IBM Service for "
                    "more information.",
            "MSG739 list items or trailing prose changed");
    for (const auto &item : node.items)
      require(!item.origin.rows.empty() && !item.origin.slices.empty(),
              "MSG739 list item lacks row/token provenance");
  }
  const auto *pre = find_block_after(*document, "MSG 508", preformatted_block);
  require(pre != nullptr,
          "MSG508 preformatted block does not follow its anchor");
  if (pre != nullptr) {
    const auto &node = std::get<geist::detail::PreformattedBlockIR>(pre->node);
    require(node.lines.size() == 11 && node.lines[0] == "Application Action" &&
                node.lines[6] == "SNMP Trap" &&
                node.lines[7] ==
                    "Verify that AIX NetView/6000 is running properly" &&
                node.lines[10] == "and requesting a window." &&
                !pre->origin.rows.empty() && !pre->origin.slices.empty(),
            "MSG508 preformatted lines or provenance changed");
  }

  const auto &final_intro =
      std::get<geist::detail::ParagraphBlockIR>(document->blocks[7].node);
  require(final_intro.content.size() == 5,
          "message DocumentIR merged the adjacent selector atoms");
  const auto *first_link = std::get_if<geist::detail::CrossReferenceInlineIR>(
      &final_intro.content[1].node);
  const auto *second_link = std::get_if<geist::detail::CrossReferenceInlineIR>(
      &final_intro.content[3].node);
  require(first_link != nullptr && second_link != nullptr &&
              first_link->label == "Chapter 2, \"Problem" &&
              second_link->label == "Determination\" in topic 2.0" &&
              first_link->target.kind ==
                  geist::detail::CrossReferenceTargetKindIR::anchor &&
              second_link->target.kind ==
                  geist::detail::CrossReferenceTargetKindIR::anchor &&
              first_link->target.value == "HDRPROBS" &&
              second_link->target.value == "HDRPROBS" &&
              std::get<geist::detail::TextInlineIR>(final_intro.content[0].node)
                      .text.find("See ") != std::string::npos &&
              std::get<geist::detail::TextInlineIR>(final_intro.content[2].node)
                      .text == " " &&
              std::get<geist::detail::TextInlineIR>(final_intro.content[4].node)
                      .text.find(" for instructions") == 0,
          "message DocumentIR lost exact selector labels, targets, or prose");

  const auto markdown = geist::detail::render_document_markdown(*document);
  require(markdown.find("<a id=\"MSG\"></a>\n\n"
                        "<a id=\"HDRMSGS\"></a>\n\n# Chapter 5\\. Messages") ==
              0,
          "message Markdown does not retain source header order");
  require(substring_count(markdown, "\n**Meaning:**") == 396 &&
              substring_count(markdown, "\n**Action:**") == 396,
          "message Markdown lost canonical section boundaries");
  require(
      markdown.find(
          "See [Chapter 2, \"Problem](<#HDRPROBS>) "
          "[Determination\" in topic 2\\.0](<#HDRPROBS>) for instructions") !=
          std::string::npos,
      "message Markdown lost the adjacent typed selector sequence");
  for (const auto *expected : {
           "stop LNM for AIX\\. Then execute ovstop followed by ovstart\\. Use "
           "ovstatus to verify the AIX NetView/6000 daemons are running\\. "
           "Restart LNM for AIX\\.",
           "Refer to the man page for usage\\.",
           "Restart the Concentrator view application",
           "concentrator view is set to unknown",
           "has been removed from the database",
       })
    require(markdown.find(expected) != std::string::npos,
            "message Markdown lost corrected source prose");
  for (const auto *artifact :
       {"LNM ? for AIX", "LNM - for AIX", "- Action", "an Action", "? SRMSG"})
    require(markdown.find(artifact) == std::string::npos,
            "message Markdown retained a source layout artifact");

  require(markdown.find("| Command type | Command |\n| --- | --- |\n"
                        "| 23006 | LAN ADP LIST SEG=\\<segment number\\> |\n") !=
                  std::string::npos &&
              markdown.find("| 31161 | LAN CAU QUERY UNIT=\\<unit id\\> "
                            "MOD=\\<module number\\> ATTR=LOBE |") !=
                  std::string::npos &&
              markdown.find("conditions are true:\n\n"
                            "- /usr/lpp/lnm/databases contains "
                            "lnmlnmemgr\\.pdf\n- /usr/lib/nls/msg/") !=
                  std::string::npos &&
              markdown.find("lnmlnmemgr\\_dfi\\.cat\n\nIf everything is "
                            "correctly set, contact IBM Service for more "
                            "information\\.") != std::string::npos &&
              markdown.find("**Action:**\n\n```\nApplication Action\n"
                            "CP Consult") != std::string::npos &&
              markdown.find("505)\nSNMP Trap\nVerify that AIX NetView/6000 "
                            "is running properly\nThen restart LNM for "
                            "AIX.\n") != std::string::npos,
          "message Markdown lost the typed table, list, or preformatted block");
  for (const auto *artifact :
       {"| action ", "| an ", "action 31096", "an 31127"})
    require(markdown.find(artifact) == std::string::npos,
            "message Markdown table leaked a structural marker spelling");

  // Structured-block conservation: dropping, duplicating, or rewriting a
  // claimed cell must fail the lowering, not silently change the output.
  auto mutated_blocks = blocks;
  for (auto &block : mutated_blocks.blocks)
    if (auto *table =
            std::get_if<geist::detail::MessageStructuredTableBlockIR>(
                &block.node))
      table->rows.erase(table->rows.begin() + 5);
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, *message, mutated_blocks, &error) &&
              error.find("has unplaced prose inside its row span") !=
                  std::string::npos,
          "message lowerer admitted a table that dropped a source row");
  mutated_blocks = blocks;
  for (auto &block : mutated_blocks.blocks)
    if (auto *table =
            std::get_if<geist::detail::MessageStructuredTableBlockIR>(
                &block.node))
      table->rows.front().cells[1].text.clear();
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, *message, mutated_blocks, &error) &&
              error.find("does not conserve the flattened section text") !=
                  std::string::npos,
          "message lowerer admitted a table that lost cell text");
  mutated_blocks = blocks;
  for (auto &block : mutated_blocks.blocks)
    if (auto *table =
            std::get_if<geist::detail::MessageStructuredTableBlockIR>(
                &block.node))
      table->rows.front().cells[1].text += " fabricated";
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, *message, mutated_blocks, &error),
          "message lowerer admitted a fabricated table cell");
  mutated_blocks = blocks;
  for (auto &block : mutated_blocks.blocks)
    if (auto *list = std::get_if<geist::detail::MessageStructuredListBlockIR>(
            &block.node))
      list->items.erase(list->items.begin());
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, *message, mutated_blocks, &error),
          "message lowerer admitted a list that dropped an item");
  mutated_blocks = blocks;
  for (auto &block : mutated_blocks.blocks)
    if (auto *pre = std::get_if<
            geist::detail::MessageStructuredPreformattedBlockIR>(&block.node))
      pre->lines[1].text += " fabricated";
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, *message, mutated_blocks, &error),
          "message lowerer admitted a fabricated preformatted line");
  mutated_blocks = blocks;
  mutated_blocks.blocks.push_back(mutated_blocks.blocks.front());
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, *message, mutated_blocks, &error),
          "message lowerer admitted two blocks for one section");
  require(!geist::detail::verify_message_topic_document_ir(
              *message, geist::detail::MessageSectionBlocksIR{}, *document),
          "message verifier admitted a document lowered with other blocks");

  auto mutated_document = *document;
  std::swap(mutated_document.blocks[0], mutated_document.blocks[1]);
  require(!geist::detail::verify_message_topic_document_ir(
              *message, blocks, mutated_document, &error) &&
              error == "message DocumentIR differs from canonical lowering",
          "message verifier admitted reordered header anchors");
  mutated_document = *document;
  auto &first_numeric_anchor =
      std::get<geist::detail::AnchorBlockIR>(mutated_document.blocks[8].node);
  first_numeric_anchor.id = "MSG changed";
  require(!geist::detail::verify_message_topic_document_ir(
              *message, blocks, mutated_document),
          "message verifier admitted a changed numeric anchor");
  mutated_document = *document;
  mutated_document.blocks[3].origin.detail = "changed provenance";
  require(!geist::detail::verify_message_topic_document_ir(
              *message, blocks, mutated_document),
          "message verifier admitted changed introduction provenance");

  auto mutated_message = *message;
  std::swap(mutated_message.anchors[2], mutated_message.anchors[3]);
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, mutated_message, blocks, &error),
          "message lowerer admitted reordered catalog anchors");
  mutated_message = *message;
  mutated_message.catalog.entries.front().sections.front().kind =
      geist::detail::MessageSectionKind::action;
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, mutated_message, blocks),
          "message lowerer admitted changed section ordering");
  mutated_message = *message;
  mutated_message.introduction.paragraphs.back().atoms[1].target.reset();
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, mutated_message, blocks),
          "message lowerer admitted a selector without its typed target");
  mutated_message = *message;
  mutated_message.heading_row_indices.front() = mutated_message.rows.size();
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, mutated_message, blocks),
          "message lowerer admitted an invalid heading row");
  mutated_message = *message;
  const auto semantic_cell = std::find_if(
      mutated_message.introduction.cells.begin(),
      mutated_message.introduction.cells.end(), [](const auto &cell) {
        return cell.role == geist::detail::MessageIntroductionCellRoleIR::text;
      });
  require(semantic_cell != mutated_message.introduction.cells.end(),
          "message fixture has no semantic introduction cell");
  const auto source_token = std::find_if(
      mutated_message.source_tokens.begin(),
      mutated_message.source_tokens.end(), [&](const auto &token) {
        return token.logical_record == semantic_cell->logical_record &&
               token.token_index == semantic_cell->token_index;
      });
  require(source_token != mutated_message.source_tokens.end(),
          "message fixture has no introduction source token");
  source_token->decoded_segment.reset();
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, mutated_message, blocks),
          "message lowerer admitted an unowned semantic introduction token");
  mutated_message = *message;
  require(
      !mutated_message.catalog.entries.front().headline.source_segments.empty(),
      "message fixture headline has no segment provenance");
  mutated_message.catalog.entries.front().headline.source_segments.front() = {
      999999, 999999};
  require(!geist::detail::lower_message_topic_to_document_ir(
              identity, mutated_message, blocks),
          "message lowerer admitted a missing paragraph source segment");

  return 0;
}
