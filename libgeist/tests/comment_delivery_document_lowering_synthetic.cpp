#include "geist/detail/comment_delivery_document_lowering.hpp"
#include "test_failures.hpp"
#include "geist/detail/internal.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace geist;
using namespace geist::detail;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "comment_delivery_document_lowering_synthetic: " << message
              << '\n';
    geist_test::record_failure();
    return;
  }
}

struct LoadedBook {
  BooDocument document;
  LogicalDecodeContext context;

  explicit LoadedBook(const std::filesystem::path &path)
      : document(BooDocument::open(path)) {
    context.bytes = read_file(path);
    const auto directory_page = read_be16(context.bytes, 0);
    const auto base = static_cast<std::size_t>(directory_page) * boo_page_size;
    context.directory.page_number = directory_page;
    context.directory.token_threshold = context.bytes[base + 0x14];
    context.directory.token_map_offset = read_be16(context.bytes, base + 0x22);
    context.directory.dictionary_start_page =
        read_be16(context.bytes, base + 0x28);
    context.directory.dictionary_page_count =
        read_be16(context.bytes, base + 0x2e);
    context.directory.logical_record_count =
        read_be16(context.bytes, base + 0x36);
    context.directory.content_page_count =
        read_be16(context.bytes, base + 0x38);
    context.directory.content_start_page =
        read_be16(context.bytes, base + 0x3a);
    context.decoded_records = decode_experimental_logical_records(
        context.bytes, context.directory, &context.record_payload_ranges);
  }
};

TopicInfo topic(const LoadedBook &book, const std::string &id) {
  const auto topics = book.document.topics();
  const auto found =
      std::find_if(topics.begin(), topics.end(),
                   [&](const auto &candidate) { return candidate.id == id; });
  require(found != topics.end(), "SC31 topic " + id + " is absent");
  return *found;
}

CommentDeliveryIR comment_ir(LoadedBook &book, const TopicInfo &topic_info) {
  const auto sources = decode_logical_record_sources(
      book.context, topic_info.start_logical_record,
      topic_info.end_logical_record);
  const auto layout = extract_layout_ir(sources);
  std::string error;
  const auto ownership =
      build_verified_ownership_ir(sources, layout, &error);
  require(ownership.has_value(),
          error.empty() ? "comment ownership is not verifiable" : error);
  const auto result =
      extract_comment_delivery_ir(sources, layout, *ownership, &error);
  require(result.has_value(),
          error.empty() ? "comment extraction failed" : error);
  // Report and continue: an empty IR is rejected by the lowerer instead of
  // dereferencing an absent value.
  return result.value_or(CommentDeliveryIR{});
}

TopicIdentityIR identity(const TopicInfo &topic_info) {
  TopicIdentityIR result;
  result.id = topic_info.id;
  result.title = topic_info.title;
  result.heading_level = topic_info.heading_level;
  result.topic_number = topic_info.topic_number;
  result.start_logical_record = topic_info.start_logical_record;
  result.end_logical_record = topic_info.end_logical_record;
  return result;
}

std::optional<const InlineIR *> paragraph_with(const DocumentIR &document,
                                               const std::string &text) {
  for (const auto &block : document.blocks) {
    const auto *paragraph = std::get_if<ParagraphBlockIR>(&block.node);
    if (paragraph == nullptr)
      continue;
    for (const auto &inline_node : paragraph->content) {
      const auto *value = std::get_if<TextInlineIR>(&inline_node.node);
      if (value != nullptr && value->text == text)
        return &inline_node;
    }
  }
  return std::nullopt;
}

bool paragraph_contains(const DocumentIR &document, const std::string &first,
                        const std::string &second) {
  for (const auto &block : document.blocks) {
    const auto *paragraph = std::get_if<ParagraphBlockIR>(&block.node);
    if (paragraph == nullptr)
      continue;
    auto saw_first = false;
    for (const auto &inline_node : paragraph->content) {
      const auto *text = std::get_if<TextInlineIR>(&inline_node.node);
      if (text == nullptr)
        continue;
      if (text->text == first)
        saw_first = true;
      if (saw_first && text->text == second)
        return true;
    }
  }
  return false;
}

bool paragraph_text_contains(const DocumentIR &document,
                             const std::string &first,
                             const std::string &second) {
  for (const auto &block : document.blocks) {
    const auto *paragraph = std::get_if<ParagraphBlockIR>(&block.node);
    if (paragraph == nullptr)
      continue;
    std::string text;
    for (const auto &inline_node : paragraph->content)
      if (const auto *value = std::get_if<TextInlineIR>(&inline_node.node))
        text += value->text;
    if (text.find(first) != std::string::npos &&
        text.find(second) != std::string::npos)
      return true;
  }
  return false;
}

bool has_synthesized_separator(const DocumentIR &document) {
  for (const auto &block : document.blocks) {
    const auto *paragraph = std::get_if<ParagraphBlockIR>(&block.node);
    if (paragraph == nullptr)
      continue;
    for (const auto &inline_node : paragraph->content) {
      const auto *text = std::get_if<TextInlineIR>(&inline_node.node);
      if (text != nullptr && text->text == " " &&
          inline_node.origin.derivation == DocumentDerivationIR::synthesized &&
          inline_node.origin.slices.empty() && inline_node.origin.rows.empty())
        return true;
    }
  }
  return false;
}

} // namespace

int main() {
  LoadedBook book(std::filesystem::path(GEIST_REPO_ROOT) / "BOO" /
                  "SC31-711.boo");
  std::string error;

  const auto back_topic = topic(book, "BACK_2");
  const auto back_source = comment_ir(book, back_topic);
  const auto back = lower_comment_delivery_to_document_ir(identity(back_topic),
                                                          back_source, &error);
  require(back.has_value(), error);
  if (!back.has_value())
    return 1;
  require(std::holds_alternative<HeadingBlockIR>(back->blocks.front().node) &&
              std::get<TextInlineIR>(
                  std::get<HeadingBlockIR>(back->blocks.front().node)
                      .content.front()
                      .node)
                      .text == "Communicating Your Comments to IBM" &&
              std::count_if(back->blocks.begin(), back->blocks.end(),
                            [](const auto &block) {
                              return std::holds_alternative<HeadingBlockIR>(
                                  block.node);
                            }) == 1 &&
              back->blocks.front().origin.slices.size() == 1 &&
              back->blocks.front().origin.rows.size() == 1 &&
              back->blocks.front().origin.rows.front().display_run == 1 &&
              back->blocks.front().origin.rows.front().row_index == 0 &&
              back->blocks.size() < 40,
          "BACK_2 title/prose did not acquire semantic document structure");
  require(verify_comment_delivery_document_ir(back_source, *back, &error),
          error);

  const auto lexical_the = paragraph_with(
      *back,
      "the comments you send should pertain to only the information in this "
      "manual");
  require(lexical_the.has_value(),
          "BACK_2 source lexical marker 'the' was not prepended");
  require((*lexical_the)->origin.slices.size() == 2 &&
              (*lexical_the)->origin.slices[0].logical_record == 541 &&
              (*lexical_the)->origin.slices[0].token_begin == 153 &&
              (*lexical_the)->origin.slices[0].token_end == 154 &&
              (*lexical_the)->origin.slices[0].byte_begin != 0 &&
              (*lexical_the)->origin.slices[1].token_begin == 156 &&
              (*lexical_the)->origin.rows.size() == 1 &&
              (*lexical_the)->origin.rows[0].display_run == 1 &&
              (*lexical_the)->origin.rows[0].row_index == 10,
          "BACK_2 lexical restoration lost marker/field/row provenance");
  require(paragraph_with(*back, "to your IBM authorized remarketer.") &&
              paragraph_with(*back, "or IBM representative for postage-paid "
                                    "mailing."),
          "BACK_2 did not restore every source-owned lexical marker");
  require(paragraph_contains(
              *back, "However,",
              "the comments you send should pertain to only the information "
              "in this manual"),
          "BACK_2 lexical continuation did not remain in its paragraph");
  require(paragraph_text_contains(
              *back,
              "If you especially like or dislike anything about this book, "
              "please use one",
              "Whichever method you choose"),
          "BACK_2 physical wraps did not remain in their prose paragraph");
  require(back_source.blocks[0].lines[2].break_before ==
                  PhysicalBreakKind::soft_wrap &&
              paragraph_contains(*back, "LAN Network Manager for AIX",
                                 "Version 1") &&
              has_synthesized_separator(*back),
          "BACK_2 soft-wrap evidence did not group source fields safely");
  require(!paragraph_with(*back, "adapter If you are mailing a readers' "
                                 "comment form (RCF) from a country other "
                                 "than"),
          "BACK_2 emitted a layout marker as lexical content");
  const auto checklist = std::find_if(back->blocks.begin(), back->blocks.end(),
                                      [](const auto &block) {
                                        return std::holds_alternative<
                                            ListBlockIR>(block.node);
                                      });
  require(checklist != back->blocks.end() &&
              std::get<ListBlockIR>(checklist->node).items.size() == 2 &&
              std::get<TextInlineIR>(
                  std::get<ListBlockIR>(checklist->node)
                      .items.front()
                      .content.front()
                      .node)
                      .text == "Title and publication number of this book",
          "BACK_2 checklist did not lower to semantic list items");

  auto changed_back = *back;
  auto &changed_text =
      std::get<TextInlineIR>(
          std::get<HeadingBlockIR>(changed_back.blocks.front().node)
              .content.front()
              .node)
          .text;
  changed_text += " changed";
  require(
      !verify_comment_delivery_document_ir(back_source, changed_back, &error) &&
          error == "comment document differs from canonical lowering",
      "comment-document verifier admitted changed semantic content");

  auto invalid_source = back_source;
  invalid_source.blocks[0].lines[10].marker.reset();
  require(!lower_comment_delivery_to_document_ir(identity(back_topic),
                                                 invalid_source, &error) &&
              error == "comment marker disposition has no marker slot",
          "comment lowerer admitted lexical content without marker source");
  invalid_source = back_source;
  invalid_source.blocks[0].lines[0].fields[0].token_begin =
      invalid_source.blocks[0].lines[0].fields[0].token_end;
  require(!lower_comment_delivery_to_document_ir(identity(back_topic),
                                                 invalid_source, &error) &&
              error == "comment source fields are out of order",
          "comment lowerer admitted an empty source-field range");

  const auto comments_topic = topic(book, "COMMENTS");
  const auto comments_source = comment_ir(book, comments_topic);
  const auto comments = lower_comment_delivery_to_document_ir(
      identity(comments_topic), comments_source, &error);
  require(comments.has_value(), error);
  if (!comments.has_value())
    return 1;
  require(
      std::holds_alternative<HeadingBlockIR>(comments->blocks.front().node) &&
          std::get<TextInlineIR>(
              std::get<HeadingBlockIR>(comments->blocks.front().node)
                  .content.front()
                  .node)
                  .text == "Help us help you!",
      "COMMENTS title did not lower to a source-proven heading");
  require(std::count_if(comments->blocks.begin(), comments->blocks.end(),
                        [](const auto &block) {
                          return std::holds_alternative<ParagraphBlockIR>(
                              block.node);
                        }) > 0 &&
              std::count_if(comments->blocks.begin(), comments->blocks.end(),
                            [](const auto &block) {
                              return std::holds_alternative<ParagraphBlockIR>(
                                  block.node);
                            }) < 10,
          "COMMENTS title prose remained split by source fields");
  // The questionnaire forms are drawn, not tabulated: hosted BookServer serves
  // `TBLUNIQ8`/`TBLUNIQ9` inside the topic's own `<pre width="80">` as the
  // underscore-and-bar box and emits no `<table>` element anywhere on the page
  // (DT 19941010174546).  Both forms and the response area therefore lower to
  // preformatted blocks.
  require(std::count_if(comments->blocks.begin(), comments->blocks.end(),
                        [](const auto &block) {
                          return std::holds_alternative<TableBlockIR>(
                              block.node);
                        }) == 0,
          "COMMENTS questionnaire objects must not lower to Markdown tables");
  require(std::count_if(comments->blocks.begin(), comments->blocks.end(),
                        [](const auto &block) {
                          return std::holds_alternative<PreformattedBlockIR>(
                              block.node);
                        }) == 3,
          "COMMENTS did not lower to two forms and one response area");

  std::vector<const BlockIR *> table_blocks;
  std::vector<const BlockIR *> anchor_blocks;
  const BlockIR *response_block = nullptr;
  for (const auto &block : comments->blocks) {
    if (std::holds_alternative<AnchorBlockIR>(block.node))
      anchor_blocks.push_back(&block);
    if (std::holds_alternative<PreformattedBlockIR>(block.node)) {
      if (table_blocks.size() < 2)
        table_blocks.push_back(&block);
      else
        response_block = &block;
    }
  }
  require(table_blocks.size() == 2 && response_block != nullptr,
          "COMMENTS typed semantic objects are absent");
  require(anchor_blocks.size() == 4 &&
              std::get<AnchorBlockIR>(anchor_blocks[0]->node).id ==
                  "TBLUNIQ8" &&
              std::get<AnchorBlockIR>(anchor_blocks[1]->node).id ==
                  "TBLTBLUNIQ8" &&
              anchor_blocks[0]->origin.slices.size() == 1 &&
              anchor_blocks[0]->origin.slices.front().logical_record == 543,
          "COMMENTS table anchors lost source identity/provenance");
  const auto &first_table =
      std::get<PreformattedBlockIR>(table_blocks[0]->node);
  const auto &second_table =
      std::get<PreformattedBlockIR>(table_blocks[1]->node);
  require(first_table.lines.size() == 2 && second_table.lines.size() == 8,
          "COMMENTS form rows retained decoration rows or changed");
  // The three proven fields of each row are laid out at one column stop, in
  // the order and the columns the source draws them; the second form's second
  // line is the continued question, whose answer cells are empty.
  require(first_table.lines[0] ==
                  "Overall, how satisfied are you with  Satisfied            "
                  "                Dissatisfied" &&
              first_table.lines[1] ==
                  "the information in this book?        __                   "
                  "                __" &&
              second_table.lines[1] == "information in this book is:" &&
              second_table.lines[2] ==
                  "Accurate                        __                        "
                  "      __" &&
              second_table.lines[7].rfind("Applicable to your task", 0) == 0,
          "COMMENTS semantic fields were not assembled into logical rows");
  require(table_blocks[0]->origin.rows.front().display_run == 3 &&
              table_blocks[1]->origin.rows.front().display_run == 8,
          "COMMENTS decoration-only table rows escaped suppression");

  const auto &lexical_response =
      std::get<PreformattedBlockIR>(response_block->node);
  require(
      lexical_response.lines.size() == 38 &&
          lexical_response.lines[18] == "information in any way you choose." &&
          lexical_response.lines[19] ==
              "Please complete this form and mail it to:" &&
          lexical_response.lines[20] ==
              "International Business Machines Corporation" &&
          response_block->origin.slices.size() == 42 &&
          std::any_of(response_block->origin.slices.begin(),
                      response_block->origin.slices.end(),
                      [](const auto &slice) {
                        return slice.logical_record == 546 &&
                               slice.token_begin == 16 &&
                               slice.token_end == 17 && slice.byte_begin != 0;
                      }) &&
          std::any_of(response_block->origin.slices.begin(),
                      response_block->origin.slices.end(),
                      [](const auto &slice) {
                        return slice.logical_record == 546 &&
                               slice.token_begin == 129 &&
                               slice.token_end == 130 &&
                               slice.byte_begin == 0x35155 &&
                               slice.byte_end == 0x35156;
                      }) &&
          response_block->origin.rows.size() == 26 &&
          response_block->origin.rows[17].display_run == 11 &&
          response_block->origin.rows[17].row_index == 17,
      "COMMENTS lexical response row lost content or provenance");
  require(
      verify_comment_delivery_document_ir(comments_source, *comments, &error),
      error);

  const auto formatted = format_document_ir(*comments);
  require(formatted.find("text=\"<\"") == std::string::npos &&
              formatted.find("line=\"<\"") == std::string::npos,
          "COMMENTS emitted source layout decoration as document content");

  std::cout << "comment delivery document lowering checks passed\n";
  return 0;
}
