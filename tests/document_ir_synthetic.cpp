#include "geist/detail/document_ir.hpp"
#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/toc.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace geist::detail;

bool require(bool condition, const std::string& message) {
  if (!condition) std::cerr << "document_ir_synthetic: " << message << '\n';
  return condition;
}

DocumentNodeOriginIR lowered() {
  DocumentNodeOriginIR result;
  result.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

InlineIR text(std::string value) {
  InlineIR result;
  result.node = TextInlineIR{std::move(value)};
  result.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

BlockIR paragraph(std::string value) {
  BlockIR result;
  result.node = ParagraphBlockIR{{text(std::move(value))}};
  result.origin.derivation = DocumentDerivationIR::semantic_lowering;
  return result;
}

} // namespace

int main() {
  TopicIdentityIR identity;
  identity.id = "INTRO";
  identity.title = "Introduction";
  identity.topic_number = 7;
  identity.start_logical_record = 100;
  identity.end_logical_record = 102;

  DocumentIR typed;
  typed.topic = identity;
  HeadingBlockIR heading;
  heading.level = 1;
  heading.content.push_back(text("Typed introduction"));
  BlockIR heading_block;
  heading_block.origin = lowered();
  heading_block.node = std::move(heading);
  typed.blocks.push_back(std::move(heading_block));
  typed.blocks.push_back(paragraph("Body"));

  TableBlockIR table;
  table.header_rows = 1;
  TableRowIR header;
  header.origin = lowered();
  header.cells.push_back(TableCellIR{{text("Name")}, lowered()});
  header.cells.push_back(TableCellIR{{text("Value")}, lowered()});
  TableRowIR body;
  body.origin = lowered();
  body.cells.push_back(TableCellIR{{text("Mode")}, lowered()});
  body.cells.push_back(TableCellIR{{text("Safe")}, lowered()});
  table.rows = {std::move(header), std::move(body)};
  BlockIR table_block;
  table_block.origin = lowered();
  table_block.node = std::move(table);
  typed.blocks.push_back(std::move(table_block));

  DocumentSourceSliceIR slice;
  slice.logical_record = 101;
  slice.segment_index = 2;
  slice.token_begin = 4;
  slice.token_end = 8;
  slice.byte_begin = 12;
  slice.byte_end = 20;
  typed.blocks[1].origin.slices.push_back(slice);
  typed.blocks[1].origin.rows.push_back(DocumentSourceRowIR{9, 3});

  std::string error;
  if (!require(verify_document_ir(typed, &error), error)) return 1;
  const auto typed_format = format_document_ir(typed);
  if (!require(typed_format.find("block 0 heading level=1") !=
                   std::string::npos,
               "formatter omitted typed heading") ||
      !require(typed_format.find("source=(lr=101 seg=2 tok=4:8 bytes=12:20)") !=
                   std::string::npos,
               "formatter omitted source slice") ||
      !require(typed_format.find("row=9:3") != std::string::npos,
               "formatter omitted row evidence"))
    return 1;

  auto invalid_heading = typed;
  std::get<HeadingBlockIR>(invalid_heading.blocks[0].node).level = 0;
  error.clear();
  if (!require(!verify_document_ir(invalid_heading, &error) &&
                   error == "heading level is outside 1..6",
               "verifier admitted invalid heading geometry"))
    return 1;

  const auto typed_markdown = render_document_markdown(typed);
  if (!require(typed_markdown ==
                   "# Typed introduction\n\nBody\n\n| Name | Value |\n"
                   "| --- | --- |\n| Mode | Safe |\n",
               "typed document did not render stable Markdown"))
    return 1;

  auto incomplete_legacy = typed;
  incomplete_legacy.topic.id.clear();
  auto rejected_invalid_document = false;
  auto rejection_message = std::string{};
  try {
    (void)render_document_markdown(incomplete_legacy);
  } catch (const std::invalid_argument& exception) {
    rejected_invalid_document = true;
    rejection_message = exception.what();
  }
  if (!require(rejected_invalid_document,
               "renderer admitted invalid DocumentIR") ||
      !require(rejection_message ==
                   "invalid DocumentIR: document topic identity is incomplete",
               "invalid DocumentIR rejection did not retain verifier detail"))
    return 1;

  auto identity_free_typed = typed;
  identity_free_typed.topic.id.clear();
  identity_free_typed.topic.title.clear();
  error.clear();
  if (!require(!verify_document_ir(identity_free_typed, &error) &&
                   error == "document topic identity is incomplete",
               "a document must always name the topic it renders"))
    return 1;

  auto invalid_table = typed;
  auto& invalid_rows =
      std::get<TableBlockIR>(invalid_table.blocks[2].node).rows;
  invalid_rows[1].cells.pop_back();
  error.clear();
  if (!require(!verify_document_ir(invalid_table, &error) &&
                   error == "table row geometry is inconsistent",
               "verifier admitted inconsistent table geometry"))
    return 1;

  auto invalid_source = typed;
  invalid_source.blocks[1].origin.slices[0].token_begin = 9;
  error.clear();
  if (!require(!verify_document_ir(invalid_source, &error) &&
                   error == "source slice has reversed token range",
               "verifier admitted reversed provenance"))
    return 1;

  auto unordered_source = typed;
  auto earlier = slice;
  earlier.token_begin = 1;
  earlier.token_end = 2;
  unordered_source.blocks[1].origin.slices.push_back(earlier);
  error.clear();
  if (!require(!verify_document_ir(unordered_source, &error) &&
                   error == "source slices are duplicated or out of order",
               "verifier admitted out-of-order provenance"))
    return 1;

  // A node that never named its source reaches verification claiming the
  // default `decoded` derivation.  It has to say `synthesized` instead.
  auto unsourced = typed;
  unsourced.blocks[1].origin = DocumentNodeOriginIR{};
  error.clear();
  if (!require(!verify_document_ir(unsourced, &error) &&
                   error == "decoded node origin names no source slice",
               "verifier admitted a node that named no source"))
    return 1;

  // A block that names its own source has to name at least the source its
  // children name.
  auto uncovered = typed;
  {
    auto child = slice;
    child.logical_record = 202;
    std::get<ParagraphBlockIR>(uncovered.blocks[1].node)
        .content.front()
        .origin.slices.push_back(child);
  }
  error.clear();
  if (!require(!verify_document_ir(uncovered, &error) &&
                   error ==
                       "block slices do not cover a child node's source slice",
               "verifier admitted a block that misses its child's source"))
    return 1;

  // Normalizing lifts the child's slice into the block that holds it.
  auto normalized = uncovered;
  normalize_document_origin_slices(normalized);
  error.clear();
  if (!require(verify_document_ir(normalized, &error), error) ||
      !require(normalized.blocks[1].origin.slices.size() == 2,
               "normalizing did not lift the child's source slice"))
    return 1;


  auto empty_identity = typed;
  empty_identity.topic.id.clear();
  error.clear();
  if (!require(!verify_document_ir(empty_identity, &error) &&
                   error == "document topic identity is incomplete",
               "verifier admitted an empty topic identity"))
    return 1;

  std::cout << "document IR synthetic checks passed\n";
  return 0;
}
