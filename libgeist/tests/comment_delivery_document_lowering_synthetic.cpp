#include "geist/detail/comment_delivery_document_lowering.hpp"
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
    std::exit(1);
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
  const auto ownership = build_ownership_ir(sources, layout);
  std::string error;
  const auto result =
      extract_comment_delivery_ir(sources, layout, ownership, &error);
  require(result.has_value(),
          error.empty() ? "comment extraction failed" : error);
  return *result;
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
  require(std::count_if(comments->blocks.begin(), comments->blocks.end(),
                        [](const auto &block) {
                          return std::holds_alternative<TableBlockIR>(
                              block.node);
                        }) == 2,
          "COMMENTS questionnaire objects did not lower to two tables");
  require(std::count_if(comments->blocks.begin(), comments->blocks.end(),
                        [](const auto &block) {
                          return std::holds_alternative<PreformattedBlockIR>(
                              block.node);
                        }) == 1,
          "COMMENTS response area did not lower to one preformatted block");

  std::vector<const BlockIR *> table_blocks;
  const BlockIR *response_block = nullptr;
  for (const auto &block : comments->blocks) {
    if (std::holds_alternative<TableBlockIR>(block.node))
      table_blocks.push_back(&block);
    if (std::holds_alternative<PreformattedBlockIR>(block.node))
      response_block = &block;
  }
  require(table_blocks.size() == 2 && response_block != nullptr,
          "COMMENTS typed semantic objects are absent");
  const auto &first_table = std::get<TableBlockIR>(table_blocks[0]->node);
  const auto &second_table = std::get<TableBlockIR>(table_blocks[1]->node);
  require(first_table.rows.size() == 2 && first_table.header_rows == 1 &&
              first_table.rows.front().cells.size() == 3 &&
              first_table.rows.front().origin.rows.size() == 3 &&
              second_table.rows.size() == 8 && second_table.header_rows == 1 &&
              second_table.rows.front().cells.size() == 3 &&
              second_table.rows[1].cells.size() == 3 &&
              second_table.rows[1].cells[1].content.empty() &&
              second_table.rows[1].cells[1].origin.derivation ==
                  DocumentDerivationIR::synthesized &&
              second_table.rows[1].cells[2].content.empty(),
          "COMMENTS table geometry retained decoration rows or changed");
  const auto cell_text = [](const TableBlockIR &table, std::size_t row,
                            std::size_t column) -> const std::string & {
    return std::get<TextInlineIR>(
               table.rows[row].cells[column].content.front().node)
        .text;
  };
  require(cell_text(first_table, 0, 0) ==
                  "Overall, how satisfied are you with" &&
              cell_text(first_table, 0, 1) == "Satisfied" &&
              cell_text(first_table, 0, 2) == "Dissatisfied" &&
              cell_text(first_table, 1, 0) ==
                  "the information in this book?" &&
              cell_text(first_table, 1, 1) == "__" &&
              cell_text(first_table, 1, 2) == "__" &&
              cell_text(second_table, 1, 0) ==
                  "information in this book is:" &&
              cell_text(second_table, 2, 0) == "Accurate" &&
              cell_text(second_table, 2, 1) == "__" &&
              cell_text(second_table, 2, 2) == "__" &&
              cell_text(second_table, 7, 0) == "Applicable to your task",
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
