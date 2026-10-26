// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Typed figure block extraction and lowering over real figures (issue #58).
//
// Positive cases are image-backed figures whose hosted BookServer rendering
// was compared by hand against the hosted edition of each book at its own
// timestamp: the anchor name, the picture resource or external path,
// the "PICTURE n" placeholder that the image replaces, and the caption.
// ASCII/CFONT-drawn figures (no picture selector) are admitted as
// preformatted figures whose body lines were compared line for line with
// the hosted <pre> output (FA1PLMM0 PREFACE.3, ACPZMST1 1.2.5, GG24-4302-00
// 3.3.4, SG24-204 3.1, SH20-918 FRONT_1.3, SC34-425 1.3.4, SC09-138 1.3.1,
// QSYSNEWG 2.1).  Negative cases prove the extractor fails closed: a picture
// selector owned by a table (GG24-395 3.3.x) is reported as table-owned
// rather than admitted as a figure, a drawn figure wrapping an SRTBL is
// declined as a table, and a region carrying a menu is declined.

#include "geist/boo.hpp"
#include "geist/detail/ir/figure_block_ir.hpp"
#include "geist/detail/lowering/figure_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/selector_ir.hpp"
#include "test_failures.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <utility>
#include <memory>
#include <set>
#include <string>
#include <algorithm>
#include <tuple>
#include <variant>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "figure_block_ir_synthetic: " << message << '\n';
    geist_test::record_failure();
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
  // The dictionary text the directory version and parameter select.
  context.directory.version_text =
      geist::detail::EbcdicCodec::cp037().decode_ascii(
          context.bytes, base + 0x10, 4, "short directory");
  context.directory.dictionary_text_parameter = static_cast<std::uint16_t>(
      (context.bytes[base + 0x4c] << 8) | context.bytes[base + 0x4d]);
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

struct Book {
  explicit Book(const std::filesystem::path &path)
      : document(geist::BooDocument::open(path)) {
    open_context(path, context);
    for (const auto &resource : document.resources())
      resource_ids.insert(resource.id);
  }
  geist::BooDocument document;
  geist::detail::LogicalDecodeContext context;
  std::set<std::string> resource_ids;
};

struct Topic {
  std::vector<geist::detail::DecodedLogicalRecordSource> sources;
  geist::detail::LayoutIR layout;
  geist::detail::OwnershipIR ownership;
  geist::detail::SelectorCatalogIR selectors;
  geist::detail::FigureBlocksIR figures;
};

class Corpus {
public:
  explicit Corpus(std::filesystem::path root) : root_(std::move(root)) {}

  const Book &book(const std::string &name) {
    auto found = books_.find(name);
    if (found == books_.end()) {
      found = books_.emplace(name, std::make_unique<Book>(root_ / name)).first;
    }
    return *found->second;
  }

  Topic topic(const std::string &book_name, const std::string &id) {
    const auto &source = book(book_name);
    const auto *entry = source.document.find_toc_entry(id);
    require(entry != nullptr, book_name + " has no topic " + id);
    Topic topic;
    if (entry == nullptr)
      return topic;
    topic.sources = geist::detail::decode_logical_record_sources(
        source.context, entry->start_logical_record,
        entry->end_logical_record);
    topic.layout = geist::detail::extract_layout_ir(topic.sources);
    topic.ownership =
        geist::detail::build_ownership_ir(topic.sources, topic.layout);
    std::string error;
    const auto selectors =
        geist::detail::extract_selector_catalog_ir(topic.sources, &error);
    // A topic without any CSELECT has no catalog; the figure extractor then
    // sees an empty one.
    require(selectors.has_value() || error == "source contains no selectors",
            book_name + " " + id + ": selector catalog rejected: " + error);
    if (selectors)
      topic.selectors = *selectors;
    topic.figures = geist::detail::extract_figure_blocks_ir(
        topic.sources, topic.layout, topic.ownership, topic.selectors,
        source.resource_ids);
    const auto verified = geist::detail::verify_figure_blocks_ir(
        topic.sources, topic.layout, topic.ownership, topic.selectors,
        source.resource_ids, topic.figures, &error);
    require(verified,
            book_name + " " + id + ": figure verification failed: " + error);
    return topic;
  }

private:
  std::filesystem::path root_;
  std::map<std::string, std::unique_ptr<Book>> books_;
};

struct Expected {
  const char *book;
  const char *topic;
  const char *anchor;
  geist::detail::FigureTargetKindIR kind;
  const char *target;
  const char *placeholder;
  const char *caption;
};

// Every cell inside a figure region is claimed exactly once, and every cell
// of a suppressed placeholder row and of the caption rows is among them.
void check_conservation(const Topic &topic, const std::string &label) {
  std::set<std::tuple<std::uint32_t, std::size_t, std::size_t>> claimed;
  for (const auto &block : topic.figures.blocks)
    for (const auto &cell : block.cells)
      require(claimed.emplace(cell.logical_record, cell.token_index,
                              cell.word_index)
                  .second,
              label + ": source cell claimed twice");
  for (const auto &block : topic.figures.blocks) {
    std::set<std::pair<std::uint64_t, std::size_t>> rows;
    for (const auto &row : block.suppressed_rows)
      rows.emplace(row.display_run, row.row_index);
    if (block.caption)
      for (const auto &row : block.caption->rows)
        rows.emplace(row.display_run, row.row_index);
    for (const auto &cell : topic.ownership.row_cells)
      if (rows.count({cell.run, cell.row_index}) != 0)
        require(claimed.count({cell.logical_record, cell.token_index,
                               cell.word_index}) != 0,
                label + ": owned row cell was not claimed");
  }
}

void check_lowering(const geist::detail::FigureSourceBlockIR &figure,
                    const std::string &label) {
  std::string error;
  auto blocks =
      geist::detail::lower_figure_block_to_document_blocks(figure, &error);
  require(blocks.has_value(), label + ": lowering failed: " + error);
  if (!blocks)
    return;
  require(geist::detail::verify_figure_document_blocks(figure, *blocks,
                                                       &error),
          label + ": lowered blocks failed verification: " + error);
  const auto expected_blocks = figure.span.anchored ? 2u : 1u;
  require(blocks->size() == expected_blocks,
          label + ": unexpected lowered block count");
  const auto *node = std::get_if<geist::detail::FigureBlockIR>(
      &blocks->back().node);
  require(node != nullptr, label + ": last lowered block is not a figure");
  if (node == nullptr)
    return;
  if (figure.span.anchored) {
    const auto *anchor = std::get_if<geist::detail::AnchorBlockIR>(
        &blocks->front().node);
    require(anchor != nullptr && anchor->id == figure.anchor,
            label + ": lowered anchor lost its id");
  }
  require(!blocks->back().origin.rows.empty() &&
              !blocks->back().origin.slices.empty(),
          label + ": lowered figure carries no provenance");

  geist::detail::DocumentIR document;
  document.topic.id = "figure";
  document.topic.title = "figure";
  document.blocks = *blocks;
  require(geist::detail::verify_document_ir(document, &error),
          label + ": document verification failed: " + error);

  // Mutations must be rejected: resource, caption text, anchor, origin.
  auto mutated = *blocks;
  std::get<geist::detail::FigureBlockIR>(mutated.back().node).resource +=
      "x";
  require(!geist::detail::verify_figure_document_blocks(figure, mutated),
          label + ": mutated resource was accepted");
  if (figure.caption) {
    mutated = *blocks;
    auto &caption =
        std::get<geist::detail::FigureBlockIR>(mutated.back().node).caption;
    std::get<geist::detail::TextInlineIR>(caption.front().node).text += "!";
    require(!geist::detail::verify_figure_document_blocks(figure, mutated),
            label + ": mutated caption was accepted");
  }
  mutated = *blocks;
  mutated.back().origin.rows.pop_back();
  require(!geist::detail::verify_figure_document_blocks(figure, mutated),
          label + ": mutated origin rows were accepted");
  if (figure.span.anchored) {
    mutated = *blocks;
    std::get<geist::detail::AnchorBlockIR>(mutated.front().node).id = "X";
    require(!geist::detail::verify_figure_document_blocks(figure, mutated),
            label + ": mutated anchor was accepted");
  }
}



// Anchor + preformatted body (+ emphasised caption paragraph), with
// provenance, verified against the canonical lowering and the document
// verifier; mutations are rejected.

const geist::detail::FigureSourceBlockIR *
find_figure(const Topic &topic, const std::string &anchor) {
  for (const auto &block : topic.figures.blocks)
    if (block.anchor == anchor)
      return &block;
  return nullptr;
}

// A drawn figure can carry a second anchor after rows have already been
// emitted, and its CSELECT can carry the six-field LNK tuple.  The source IR
// keeps the exact row coordinate. DocumentIR places the destination before
// that row inside the same preformatted body; the lowered verbatim link
// retains every alternative for the consumer resolver.
void test_positioned_spot_anchor_and_lnk() {
  using namespace geist::detail;
  FigureSourceBlockIR figure;
  figure.span = {{7, 0}, {7, 3}, true};
  figure.anchor = "FIGDRAWN";
  figure.body_kind = FigureBodyKindIR::preformatted;

  FigurePreformattedLineIR before;
  before.logical_record = 7;
  before.prefix_token = 1;
  before.token_end = 3;
  before.text = "before";
  before.rows.push_back({1, 0});
  for (std::size_t offset = 0; offset <= before.text.size(); ++offset)
    before.column_offsets.push_back(offset);
  figure.lines.push_back(before);

  FigurePreformattedLineIR after;
  after.logical_record = 7;
  after.prefix_token = 4;
  after.token_end = 6;
  after.text = "open BOOK";
  after.rows.push_back({1, 1});
  for (std::size_t offset = 0; offset <= after.text.size(); ++offset)
    after.column_offsets.push_back(offset);
  figure.lines.push_back(after);

  figure.spot_anchors.push_back({"SPTMIDDLE", 7, 1, 1});

  FigureLinkIR link;
  link.selector = {7, 2, 0};
  link.target = "TARGETID";
  link.kind = FigureLinkKindIR::book_heading;
  link.alternatives = {"HDR", "HEADING", "", "SC00-0000", "ANY",
                       "TARGETID"};
  link.document_number = "SC00-0000";
  link.document_level = "ANY";
  link.heading_anchor = "HEADING";
  link.logical_record = 7;
  link.line_prefix_token = 4;
  link.column = 5;
  link.length = 4;
  link.label = "BOOK";
  link.source = {7, 2, 6, 7, 16, 18};
  figure.body_links.push_back(link);

  const auto cell = [](std::size_t segment, std::size_t token,
                       FigureCellRoleIR role) {
    FigureSourceCellIR result;
    result.logical_record = 7;
    result.segment_index = segment;
    result.token_index = token;
    result.word = 'x';
    result.token_bytes = {static_cast<std::uint32_t>(token * 2),
                          static_cast<std::uint32_t>(token * 2 + 2)};
    result.role = role;
    return result;
  };
  figure.cells = {
      cell(0, 0, FigureCellRoleIR::control),
      cell(0, 1, FigureCellRoleIR::line_prefix),
      cell(0, 2, FigureCellRoleIR::body_content),
      cell(1, 3, FigureCellRoleIR::control),
      cell(2, 4, FigureCellRoleIR::line_prefix),
      cell(2, 5, FigureCellRoleIR::body_content),
      cell(2, 6, FigureCellRoleIR::control),
  };

  std::string error;
  const auto blocks = lower_figure_block_to_document_blocks(figure, &error);
  require(blocks.has_value(), "positioned figure lowering failed: " + error);
  if (!blocks)
    return;
  require(blocks->size() == 2,
          "positioned figure did not lower to one anchor and one body");
  const auto *body = std::get_if<PreformattedBlockIR>(&(*blocks)[1].node);
  require(body && body->line_links.size() == 2 &&
              body->line_links[1].size() == 1,
          "positioned figure lost its drawn-body LNK selector");
  require(body && body->line_anchors.size() == 1 &&
              body->line_anchors.front().line == 1 &&
              body->line_anchors.front().anchor.id == "SPTMIDDLE" &&
              body->line_anchors.front().origin.slices.size() == 1 &&
              body->line_anchors.front().origin.slices.front().token_begin == 3 &&
              body->line_anchors.front().origin.slices.front().token_end == 4,
          "positioned figure anchor did not retain its line/source coordinate");
  if (body && body->line_links.size() == 2 &&
      body->line_links[1].size() == 1) {
    const auto &lowered = body->line_links[1].front();
    require(lowered.kind == VerbatimLinkKindIR::book_heading &&
                lowered.target == "TARGETID" &&
                lowered.alternatives == link.alternatives &&
                lowered.document_number == "SC00-0000" &&
                lowered.document_level == "ANY" &&
                lowered.heading_anchor == "HEADING",
            "positioned figure collapsed the LNK alternative tuple");
  }
  require((*blocks)[1].origin.fidelity == DocumentFidelityIR::typed,
          "represented figure body anchor remained degraded");
  const auto html = render_document_html_blocks(*blocks, {});
  const auto markdown = render_document_markdown(
      DocumentIR{{"TEST", "Positioned anchor"}, *blocks});
  const auto html_before = html.find("before\n");
  const auto html_anchor = html.find("id=\"SPTMIDDLE\"");
  const auto html_after = html.find("open <a");
  require(html.find("<pre") != std::string::npos &&
              html_before < html_anchor && html_anchor < html_after &&
              html.find("<pre", html.find("<pre") + 1) == std::string::npos,
          "HTML moved the positioned anchor outside its single body: " + html);
  const auto md_before = markdown.find("before\n");
  const auto md_anchor = markdown.find("<a id=\"SPTMIDDLE\"></a>");
  const auto md_after = markdown.find("open <a");
  require(md_before < md_anchor && md_anchor < md_after &&
              markdown.find("<pre", markdown.find("<pre") + 1) ==
                  std::string::npos,
          "Markdown moved the positioned anchor outside its single body: " +
              markdown);
  require(verify_figure_document_blocks(figure, *blocks, &error),
          "positioned figure lowering verification failed: " + error);

  auto mutated = *blocks;
  auto &mutated_body = std::get<PreformattedBlockIR>(mutated[1].node);
  mutated_body.line_links[1][0].alternatives[3] = "SC99-9999";
  require(!verify_figure_document_blocks(figure, mutated),
          "mutated LNK alternative was accepted");
  mutated = *blocks;
  ++std::get<PreformattedBlockIR>(mutated[1].node).line_anchors[0].line;
  require(!verify_figure_document_blocks(figure, mutated),
          "mutated positioned anchor line was accepted");
}

// A drawn figure's `LNK <SPT>` selector links a spot in another book.
// SC31-6209-00 1.2.1.1 record 165 draws the `OPTIONS FASTRUN` box between
// `SRFIGFIGUNIQ12` and `SREFIG` and selects `FASTRUN` with
// `cselect 15 7 LNK <SPT> <OFASTRU> <> <SC31-6210-00> <> <RDR>`; hosted
// (DT 19920811184856) serves it inside the figure's `<pre>` as
// `<a href="../../DOCNUM/SC31-6210-00/SPTOFASTRU?ScrollTOP=SPTOFASTRU#SPTOFASTRU">FASTRUN</a>`.
// The verbatim link keeps the spot anchor for the consumer's resolver.
// `figure_kind` selects the cross-book kind: the `<SPT>` link of the source,
// or the same box selecting a figure of the other book with `<FIG>`, which
// shares the plumbing and hands `FIG<anchor>` to the consumer.
void test_drawn_figure_spot_link(bool figure_kind = false) {
  using namespace geist::detail;
  FigureSourceBlockIR figure;
  figure.span = {{165, 0}, {165, 2}, true};
  figure.anchor = "FIGFIGUNIQ12";
  figure.body_kind = FigureBodyKindIR::preformatted;

  FigurePreformattedLineIR row;
  row.logical_record = 165;
  row.prefix_token = 1;
  row.token_end = 4;
  row.text = "   | OPTIONS   FASTRUN |";
  row.rows.push_back({1, 0});
  for (std::size_t offset = 0; offset <= row.text.size(); ++offset)
    row.column_offsets.push_back(offset);
  figure.lines.push_back(row);

  FigureLinkIR link;
  link.selector = {165, 1, 0};
  link.target = "RDR";
  link.kind = figure_kind ? FigureLinkKindIR::book_anchor
                          : FigureLinkKindIR::book_spot;
  link.alternatives = {figure_kind ? "FIG" : "SPT", "OFASTRU", "",
                       "SC31-6210-00", "", "RDR"};
  link.document_number = "SC31-6210-00";
  if (figure_kind)
    link.book_anchor = "FIGOFASTRU";
  else
    link.spot_anchor = "OFASTRU";
  link.logical_record = 165;
  link.line_prefix_token = 1;
  link.column = 15;
  link.length = 7;
  link.label = "FASTRUN";
  link.source = {165, 1, 5, 6, 10, 12};
  figure.body_links.push_back(link);

  const auto cell = [](std::size_t segment, std::size_t token,
                       FigureCellRoleIR role) {
    FigureSourceCellIR result;
    result.logical_record = 165;
    result.segment_index = segment;
    result.token_index = token;
    result.word = 'x';
    result.token_bytes = {static_cast<std::uint32_t>(token * 2),
                          static_cast<std::uint32_t>(token * 2 + 2)};
    result.role = role;
    return result;
  };
  figure.cells = {
      cell(0, 0, FigureCellRoleIR::control),
      cell(2, 1, FigureCellRoleIR::line_prefix),
      cell(2, 2, FigureCellRoleIR::body_content),
      cell(1, 5, FigureCellRoleIR::control),
  };

  std::string error;
  const auto blocks = lower_figure_block_to_document_blocks(figure, &error);
  require(blocks.has_value(), "spot-link figure lowering failed: " + error);
  if (!blocks)
    return;
  const auto *body = std::get_if<PreformattedBlockIR>(&blocks->back().node);
  require(body && body->line_links.size() == 1 &&
              body->line_links[0].size() == 1,
          "the drawn figure lost its LNK <SPT> selector");
  if (!body || body->line_links.size() != 1 || body->line_links[0].size() != 1)
    return;
  const auto &lowered = body->line_links[0].front();
  require((figure_kind ? lowered.kind == VerbatimLinkKindIR::book_anchor &&
                             lowered.book_anchor == "FIGOFASTRU" &&
                             lowered.spot_anchor.empty()
                       : lowered.kind == VerbatimLinkKindIR::book_spot &&
                             lowered.spot_anchor == "OFASTRU" &&
                             lowered.book_anchor.empty()) &&
              lowered.document_number == "SC31-6210-00" &&
              lowered.heading_anchor.empty() &&
              lowered.alternatives == link.alternatives,
          "the spot link lost its kind or anchor");
  std::vector<geist::HtmlCrossBookReference> asked;
  geist::HtmlRenderOptions options;
  options.resolve_cross_book =
      [&asked](const geist::HtmlCrossBookReference &reference)
      -> std::optional<std::string> {
    asked.push_back(reference);
    return "/shelf/" + reference.document_number + "/" +
           (reference.anchor.empty() ? "SPT" + reference.spot_anchor
                                     : reference.anchor);
  };
  const auto html = render_document_html_blocks(*blocks, options);
  require(asked.size() == 1 &&
              html.find(figure_kind
                            ? "href=\"/shelf/SC31-6210-00/FIGOFASTRU\""
                            : "href=\"/shelf/SC31-6210-00/SPTOFASTRU\"") !=
                  std::string::npos &&
              html.find(figure_kind ? "geist-link--book-anchor"
                                    : "geist-link--book-spot") !=
                  std::string::npos &&
              html.find(">FASTRUN</a>") != std::string::npos,
          "the resolved spot link is not on FASTRUN: " + html);
  const auto markdown = render_document_markdown(
      DocumentIR{{"1.2.1.1", "Verifying"}, *blocks});
  require(markdown.find("FASTRUN") != std::string::npos &&
              markdown.find("OFASTRU") == std::string::npos,
          "Markdown lost the label or leaked the tuple: " + markdown);
  require(verify_figure_document_blocks(figure, *blocks, &error),
          "spot-link figure lowering verification failed: " + error);
}

// Caption text and its selector are separate source claims.  In SH20-918
// 2.1/2.5 the CSELECT operands sort before the caption content they annotate;
// lowering must not fold those control bytes into the visible label origin.
void test_caption_selector_provenance() {
  using namespace geist::detail;
  FigureSourceBlockIR figure;
  figure.span = {{19, 1}, {19, 1}, false};
  figure.body_kind = FigureBodyKindIR::picture;
  figure.target = "7";
  figure.placeholder_text = "PICTURE 7";

  const auto cell = [](std::size_t segment, std::size_t token,
                       FigureCellRoleIR role) {
    FigureSourceCellIR result;
    result.logical_record = 19;
    result.segment_index = segment;
    result.token_index = token;
    result.word = 'x';
    result.token_bytes = {static_cast<std::uint32_t>(token * 2),
                          static_cast<std::uint32_t>(token * 2 + 2)};
    result.role = role;
    return result;
  };
  figure.cells = {
      cell(1, 2, FigureCellRoleIR::control),
      cell(2, 8, FigureCellRoleIR::caption_content),
  };

  FigureCaptionIR caption;
  caption.text = "See Figure 4";
  caption.rows.push_back({3, 0});
  FigureCaptionIR::Span span;
  span.begin = 4;
  span.end = caption.text.size();
  span.link.selector = {19, 1, 0};
  span.link.target = "FIGTITEM";
  span.link.column = 4;
  span.link.length = 8;
  span.link.label = "Figure 4";
  span.link.source = {19, 1, 2, 3, 4, 6};
  caption.links.push_back(span);
  figure.caption = caption;

  std::string error;
  const auto blocks = lower_figure_block_to_document_blocks(figure, &error);
  require(blocks.has_value(), "caption selector lowering failed: " + error);
  if (!blocks)
    return;
  const auto *body = std::get_if<FigureBlockIR>(&blocks->back().node);
  require(body != nullptr && body->caption.size() == 2,
          "caption selector did not split the visible caption");
  if (body == nullptr || body->caption.size() != 2)
    return;
  const auto *reference =
      std::get_if<CrossReferenceInlineIR>(&body->caption[1].node);
  require(reference != nullptr && reference->targets.size() == 1,
          "caption selector did not lower to one cross-reference claim");
  if (reference == nullptr || reference->targets.size() != 1)
    return;
  const auto &claim = reference->targets.front();
  require(claim.control_origin.has_value() &&
              claim.control_origin->slices ==
                  std::vector<DocumentSourceSliceIR>{span.link.source} &&
              claim.control_origin->rows.empty(),
          "caption claim provenance includes visible caption content");
  require(body->caption[1].origin.slices.size() == 1 &&
              !(body->caption[1].origin.slices.front() == span.link.source) &&
              body->caption[1].origin.slices.front().token_begin == 8,
          "caption label provenance includes selector operands");
  require(verify_document_ir(
              DocumentIR{{"TEST", "caption selector"}, *blocks}, &error),
          "caption selector produced invalid DocumentIR: " + error);
  const auto html = render_document_html_blocks(*blocks, {});
  const auto markdown = render_document_markdown(
      DocumentIR{{"TEST", "caption selector"}, *blocks});
  require(html.find("href=\"#FIGTITEM\">Figure 4</a>") !=
              std::string::npos,
          "HTML lost the figure-caption cross-reference: " + html);
  require(markdown.find("[Figure 4](<#FIGTITEM>)") != std::string::npos,
          "Markdown lost the figure-caption cross-reference: " + markdown);
}


} // namespace

int main() {
  test_positioned_spot_anchor_and_lnk();
  test_drawn_figure_spot_link();
  test_drawn_figure_spot_link(true);
  test_caption_selector_provenance();
  Corpus corpus(std::filesystem::path(GEIST_FIXTURE_DIR));
  using geist::detail::FigureTargetKindIR;

  // Hosted-verified figures (packet DT 20260614112503).  All nine of packet's
  // figures draw a stored book resource; the external-image, drawn/ASCII,
  // captionless, change-bar-caption, wrapped-caption, cross-record-body,
  // caption-cross-reference, body-cross-reference and table-owned-picture
  // fixtures went with the books that cannot be published (issue #59).
  const Expected expected[] = {
      {"packet.boo", "1.3", "FIGFIGUNIQ5", FigureTargetKindIR::book_resource,
       "1", "PICTURE 1", "Figure 1. VHF/UHF LMR audio frequency range"},
      {"packet.boo", "2.1.3", "FIGFIGUNIQ7", FigureTargetKindIR::book_resource,
       "2", "PICTURE 2", "Figure 2. AX.25 frame structure"},
      // A figure whose placeholder row is suppressed in its own right.
      {"packet.boo", "2.2.1", "FIGFIGUNIQ15",
       FigureTargetKindIR::book_resource, "5", "PICTURE 5",
       "Figure 5. Packet radio protocol layers"},
      {"packet.boo", "2.4", "FIGFIGUNIQ16", FigureTargetKindIR::book_resource,
       "6", "PICTURE 6", "Figure 6. IPv4 and IPv6 Packets"},
      {"packet.boo", "3.3.2", "FIGFIGUNIQ30",
       FigureTargetKindIR::book_resource, "7", "PICTURE 7",
       "Figure 7. IP Routing Example"},
      {"packet.boo", "3.3.3", "FIGFIGUNIQ32",
       FigureTargetKindIR::book_resource, "8", "PICTURE 8",
       "Figure 8. IP NAT Example"},
      {"packet.boo", "7.1.3", "FIGFIGUNIQ80",
       FigureTargetKindIR::book_resource, "9", "PICTURE 9",
       "Figure 9. LoRa Frame Format"},
  };
  for (const auto &item : expected) {
    const std::string label = std::string(item.book) + " " + item.topic;
    const auto topic = corpus.topic(item.book, item.topic);
    require(topic.figures.blocks.size() == 1,
            label + ": expected exactly one admitted figure, got " +
                std::to_string(topic.figures.blocks.size()) + "\n" +
                geist::detail::format_figure_blocks_ir(topic.figures));
    const auto *figure = find_figure(topic, item.anchor);
    require(figure != nullptr, label + ": figure anchor was not admitted");
    if (figure == nullptr)
      continue;
    require(figure->span.anchored, label + ": figure lost its anchor span");
    require(figure->target_kind == item.kind && figure->target == item.target,
            label + ": figure target changed: " + figure->target);
    require(figure->placeholder_text == item.placeholder,
            label + ": placeholder changed: '" + figure->placeholder_text +
                "'");
    require(figure->caption && figure->caption->text == item.caption,
            label + ": caption changed: '" +
                (figure->caption ? figure->caption->text : "") + "'");
    // The "PICTURE n" placeholder is either its own suppressed row or shares
    // the caption row; either way its cells are claimed.
    require(!figure->suppressed_rows.empty() ||
                (figure->caption && !figure->caption->rows.empty()),
            label + ": placeholder row was not claimed");
    check_conservation(topic, label);
    check_lowering(*figure, label);

    // Mutating the extraction must be rejected by the verifier.
    auto mutated = topic.figures;
    mutated.blocks.front().target += "1";
    std::string error;
    const auto &book = corpus.book(item.book);
    require(!geist::detail::verify_figure_blocks_ir(
                topic.sources, topic.layout, topic.ownership, topic.selectors,
                book.resource_ids, mutated, &error),
            label + ": mutated figure target was accepted");
    mutated = topic.figures;
    mutated.blocks.front().cells.pop_back();
    require(!geist::detail::verify_figure_blocks_ir(
                topic.sources, topic.layout, topic.ownership, topic.selectors,
                book.resource_ids, mutated, &error),
            label + ": figure with a dropped cell was accepted");
    if (figure->content.size() > 1) {
      mutated = topic.figures;
      std::swap(mutated.blocks.front().content.front(),
                mutated.blocks.front().content.back());
      require(!geist::detail::verify_figure_blocks_ir(
                  topic.sources, topic.layout, topic.ownership, topic.selectors,
                  book.resource_ids, mutated, &error),
              label + ": reordered picture content was accepted");
      mutated = topic.figures;
      mutated.blocks.front().content.pop_back();
      require(!geist::detail::verify_figure_blocks_ir(
                  topic.sources, topic.layout, topic.ownership, topic.selectors,
                  book.resource_ids, mutated, &error),
              label + ": dropped picture content was accepted");
    }
    mutated = topic.figures;
    mutated.blocks.front().cells.front().role =
        geist::detail::FigureCellRoleIR::caption_content;
    require(!geist::detail::verify_figure_blocks_ir(
                topic.sources, topic.layout, topic.ownership, topic.selectors,
                book.resource_ids, mutated, &error),
            label + ": figure with a re-rolled cell was accepted");
  }

  // Two consecutive figures in one topic: each keeps its own anchor, target,
  // placeholder and caption, and the region conserves every cell of both.
  {
    const auto topic = corpus.topic("packet.boo", "2.1.4");
    require(topic.figures.blocks.size() == 2,
            "packet 2.1.4: expected two admitted figures\n" +
                geist::detail::format_figure_blocks_ir(topic.figures));
    const auto *kiss = find_figure(topic, "FIGFIGUNIQ11");
    const auto *hdlc = find_figure(topic, "FIGFIGUNIQ12");
    require(kiss != nullptr && kiss->target == "3" && kiss->caption &&
                kiss->caption->text == "Figure 3. KISS + AX.25 frame structure",
            "packet 2.1.4: the first of two figures changed");
    require(hdlc != nullptr && hdlc->target == "4" && hdlc->caption &&
                hdlc->caption->text == "Figure 4. HDLC + AX.25 frame structure",
            "packet 2.1.4: the second of two figures changed");
    check_conservation(topic, "packet.boo 2.1.4");
    if (kiss != nullptr)
      check_lowering(*kiss, "packet.boo 2.1.4 FIGFIGUNIQ11");
    if (hdlc != nullptr)
      check_lowering(*hdlc, "packet.boo 2.1.4 FIGFIGUNIQ12");
  }

  // Without a resource catalog a book-resource figure must be declined.
  {
    const auto topic = corpus.topic("packet.boo", "2.4");
    const auto figures = geist::detail::extract_figure_blocks_ir(
        topic.sources, topic.layout, topic.ownership, topic.selectors, {});
    require(figures.blocks.empty() && figures.declined.size() == 1 &&
                figures.declined.front().reason.find(
                    "not in the resource catalog") != std::string::npos,
            "figure with an unknown resource was admitted");
  }

  // Every figure the book names in its generated FIGURES list is admitted by
  // the family, and no other topic invents one.  This is the whole-book
  // inventory the corpus sweep used to provide.
  {
    const auto &book = corpus.book("packet.boo");
    std::size_t admitted = 0;
    for (const auto &entry : book.document.table_of_contents()) {
      if (entry.id == "FIGURES" || entry.id == "TABLES" ||
          entry.id == "CONTENTS" || entry.id == "INDEX")
        continue;
      const auto topic = corpus.topic("packet.boo", entry.id);
      admitted += topic.figures.blocks.size();
      for (const auto &declined : topic.figures.declined)
        require(false, "packet " + entry.id + ": a figure was declined: " +
                           declined.reason);
    }
    require(admitted == 9,
            "packet admitted " + std::to_string(admitted) +
                " figures, not the nine its FIGURES list names");
  }

  std::cout << "figure block IR checks passed\n";
  return 0;
}
