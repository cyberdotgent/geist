// Typed figure block extraction and lowering over real figures (issue #58).
//
// Positive cases are image-backed figures whose hosted BookServer rendering
// was compared by hand (AnalysisNotes/typed-route-census-2026-08-28.md lists
// the DT values): the anchor name, the picture resource or external path,
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
#include "geist/detail/figure_block_ir.hpp"
#include "geist/detail/figure_document_lowering.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/selector_ir.hpp"
#include "test_failures.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <utility>
#include <memory>
#include <set>
#include <string>
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

struct PreformattedExpected {
  const char *book;
  const char *topic;
  const char *anchor;
  std::size_t line_count;
  const char *caption;
  std::vector<std::pair<std::size_t, std::string>> lines;
};

// Anchor + preformatted body (+ emphasised caption paragraph), with
// provenance, verified against the canonical lowering and the document
// verifier; mutations are rejected.
void check_preformatted_lowering(const geist::detail::FigureSourceBlockIR &figure,
                                 const std::string &label) {
  std::string error;
  auto blocks =
      geist::detail::lower_figure_block_to_document_blocks(figure, &error);
  require(blocks.has_value(), label + ": lowering failed: " + error);
  if (!blocks)
    return;
  require(geist::detail::verify_figure_document_blocks(figure, *blocks, &error),
          label + ": lowered blocks failed verification: " + error);
  const auto expected_blocks = figure.caption ? 3u : 2u;
  require(blocks->size() == expected_blocks,
          label + ": unexpected lowered block count");
  if (blocks->size() != expected_blocks)
    return;
  const auto *anchor =
      std::get_if<geist::detail::AnchorBlockIR>(&(*blocks)[0].node);
  require(anchor != nullptr && anchor->id == figure.anchor,
          label + ": lowered anchor lost its id");
  const auto *body =
      std::get_if<geist::detail::PreformattedBlockIR>(&(*blocks)[1].node);
  require(body != nullptr && body->lines.size() == figure.lines.size(),
          label + ": lowered body is not the preformatted block");
  if (body != nullptr)
    for (std::size_t index = 0; index < figure.lines.size(); ++index)
      require(body->lines[index] == figure.lines[index].text,
              label + ": lowered body line differs");
  require(!(*blocks)[1].origin.slices.empty(),
          label + ": lowered body carries no provenance");
  if (figure.caption) {
    const auto *paragraph =
        std::get_if<geist::detail::ParagraphBlockIR>(&(*blocks)[2].node);
    require(paragraph != nullptr && paragraph->content.size() == 1,
            label + ": lowered caption is not a paragraph");
    if (paragraph != nullptr && paragraph->content.size() == 1) {
      const auto *emphasis = std::get_if<geist::detail::EmphasisInlineIR>(
          &paragraph->content.front().node);
      require(emphasis != nullptr && emphasis->text == figure.caption->text,
              label + ": lowered caption text differs");
    }
  }
  geist::detail::DocumentIR document;
  document.topic.id = "figure";
  document.topic.title = "figure";
  document.blocks = *blocks;
  require(geist::detail::verify_document_ir(document, &error),
          label + ": document verification failed: " + error);

  auto mutated = *blocks;
  std::get<geist::detail::PreformattedBlockIR>(mutated[1].node).lines.back() +=
      "x";
  require(!geist::detail::verify_figure_document_blocks(figure, mutated),
          label + ": mutated body line was accepted");
  mutated = *blocks;
  mutated[1].origin.slices.pop_back();
  require(!geist::detail::verify_figure_document_blocks(figure, mutated),
          label + ": mutated body origin was accepted");
  if (figure.caption) {
    mutated = *blocks;
    std::get<geist::detail::EmphasisInlineIR>(
        std::get<geist::detail::ParagraphBlockIR>(mutated[2].node)
            .content.front()
            .node)
        .text += "!";
    require(!geist::detail::verify_figure_document_blocks(figure, mutated),
            label + ": mutated caption was accepted");
  }
}

const geist::detail::FigureSourceBlockIR *
find_figure(const Topic &topic, const std::string &anchor) {
  for (const auto &block : topic.figures.blocks)
    if (block.anchor == anchor)
      return &block;
  return nullptr;
}

bool declined_with(const Topic &topic, const std::string &fragment) {
  for (const auto &declined : topic.figures.declined)
    if (declined.reason.find(fragment) != std::string::npos)
      return true;
  return false;
}

} // namespace

int main() {
  Corpus corpus(std::filesystem::path(GEIST_REPO_ROOT) / "BOO");
  using geist::detail::FigureTargetKindIR;

  // Hosted-verified figures.  DT values: GG24-4302-00 19950308184737,
  // XWEBDEMO 19970423182524, SC24-5520-00 19911011135123,
  // SH12-565 19941206115523, SC09-138 19910321130500.
  const Expected expected[] = {
      {"GG24-4302-00.boo", "5.1.8", "FIG4302RSX",
       FigureTargetKindIR::book_resource, "9", "PICTURE 9",
       "Figure 20. RSR Components"},
      {"GG24-4302-00.boo", "8.5.3", "FIG4302TC0",
       FigureTargetKindIR::book_resource, "25", "PICTURE 25",
       "Figure 42. TCP/IP Access"},
      {"XWEBDEMO.boo", "1.4.1", "FIGMONET1",
       FigureTargetKindIR::external_image, "/bookmgr/monetcoq.jpg", "",
       "Figure 3. External JPEG format image presented in-line."},
      // Caption row opens with a subject-index entry ("SI DCE, file system")
      // that hosted output never shows.
      {"GG24-395.boo", "2.4.4", "FIGFDCE107",
       FigureTargetKindIR::book_resource, "19", "PICTURE 19",
       "Figure 19. DCE File System"},
      // Placeholder and caption share one physical row.
      {"SC09-2417-00.boo", "1.2.2", "FIGCRTP",
       FigureTargetKindIR::book_resource, "3", "PICTURE 3",
       "Figure 3. Program Creation in ILE"},
      {"FA1PLMM0.boo", "2.2.2.2", "FIGFDYN1",
       FigureTargetKindIR::book_resource, "1", "PICTURE 1",
       "Figure 6. VSE/ESA System with Static and Dynamic Partitions "
       "(Environment 4, ESA Mode)"},
      // Box filler decoded as the unmapped word 0xFFFF.
      {"SG24-204.boo", "1.3.2", "FIGFIGUNIQ1",
       FigureTargetKindIR::book_resource, "1", "PICTURE 1",
       "Figure 1. Comparing ECI and EPI."},
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
    // the caption row (SC09-2417-00 1.2.2); either way its cells are claimed.
    require(!figure->suppressed_rows.empty() ||
                (figure->caption && !figure->caption->rows.empty()) ||
                item.kind == FigureTargetKindIR::external_image,
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
    mutated = topic.figures;
    mutated.blocks.front().cells.front().role =
        geist::detail::FigureCellRoleIR::caption_content;
    require(!geist::detail::verify_figure_blocks_ir(
                topic.sources, topic.layout, topic.ownership, topic.selectors,
                book.resource_ids, mutated, &error),
            label + ": figure with a re-rolled cell was accepted");
  }

  // Without a resource catalog a book-resource figure must be declined.
  {
    const auto topic = corpus.topic("GG24-4302-00.boo", "5.1.8");
    const auto figures = geist::detail::extract_figure_blocks_ir(
        topic.sources, topic.layout, topic.ownership, topic.selectors, {});
    require(figures.blocks.empty() && figures.declined.size() == 1 &&
                figures.declined.front().reason.find(
                    "not in the resource catalog") != std::string::npos,
            "figure with an unknown resource was admitted");
  }

  // Subject-index entries inside the region are conserved, not displayed.
  {
    const auto topic = corpus.topic("GG24-395.boo", "2.4.4");
    const auto *figure = find_figure(topic, "FIGFDCE107");
    require(figure != nullptr && figure->index_terms.size() == 1 &&
                figure->index_terms.front() == "SI DCE, file system",
            "GG24-395 2.4.4: subject-index entry was not conserved");
  }

  // Hosted QSYSNEWG 1.1 (DT 19910524085706): anchored picture, no caption.
  {
    const auto topic = corpus.topic("QSYSNEWG.BOO", "1.1");
    const auto *figure = find_figure(topic, "FIGFIGUNIQ1");
    require(topic.figures.blocks.size() == 1 && figure != nullptr &&
                figure->target == "1" && figure->placeholder_text == "PICTURE 1" &&
                !figure->caption,
            "QSYSNEWG 1.1: captionless figure changed");
    if (figure != nullptr)
      check_lowering(*figure, "QSYSNEWG.BOO 1.1");
  }

  // Hosted SC24-5527-02 1.5 (DT 19921218151459): five consecutive figures
  // whose captions carry a change bar ("| Figure  1-4. ...").
  {
    const auto topic = corpus.topic("SC24-5527-02.boo", "1.5");
    require(topic.figures.blocks.size() == 5,
            "SC24-5527-02 1.5: expected five admitted figures\n" +
                geist::detail::format_figure_blocks_ir(topic.figures));
    const auto *figure = find_figure(topic, "FIGDCRDISK");
    require(figure != nullptr && figure->target == "4" && figure->caption &&
                figure->caption->text ==
                    "Figure 1-4. Disks Used When Servicing CMS and REXX/VM",
            "SC24-5527-02 1.5: change-bar caption changed");
    check_conservation(topic, "SC24-5527-02.boo 1.5");
  }

  // Negative: picture selectors inside GG24-395 3.3.x tables are
  // table-owned, never standalone figures.
  for (const auto *id : {"3.3.7", "3.3.11", "3.3.15"}) {
    const auto topic = corpus.topic("GG24-395.boo", id);
    require(topic.figures.blocks.empty(),
            std::string("GG24-395 ") + id +
                ": table-owned picture admitted as a figure");
    require(declined_with(topic, "table-owned"),
            std::string("GG24-395 ") + id +
                ": table-owned picture was not reported");
  }

  // Preformatted (ASCII/CFONT-drawn) figures.  Every expected line below is
  // the hosted <pre> line (DT values as in AnalysisNotes); the body line
  // count is the hosted count between the frame and the caption.
  const PreformattedExpected drawn[] = {
      // CFONT-boxed prose; no caption; the top corners are blanks, the
      // bottom corners bars, exactly as hosted draws them.
      {"FA1PLMM0.boo", "PREFACE.3", "FIGFIGUNIQ1", 10, "",
       {{0, "    " + std::string(72, '_') + " "},
        {2, "   | The manual VSE/ESA Networking Support has planning "
            "information for     |"},
        {9, "   |" + std::string(72, '_') + "|"}}},
      // Two-box diagram; arrows keep their glyphs (hosted shows the
      // reader's substitution bytes there).
      {"ACPZMST1.boo", "1.2.5", "FIGRSCOM2", 13,
       "Figure 2. Communications between a User Program and a Resource "
       "Manager",
       {{0, "                ______________             ______________ "},
        {2, "               | | User     | |           | | Resource | |"},
        {3, "    Requester  | | Program  | \xE2\x86\x90___________\xE2\x86\x92 | "
            "Manager  | |   Server"},
        {9, "                   NetWare                    OS/2,"}}},
      // Region spans logical records 56 and 57.
      {"ACPZMST1.boo", "1.2.5", "FIGCOMP", 17,
       "Figure 3. Communications between Programs on Different Systems",
       {{2, "   |__________________________|       "
            "|__________________________|"},
        {5, "    __________________________          "
            "_________________________ "}}},
      // 132-column RMF report inside a 72-column box: the right border
      // overwrites the report in the stream; the caption wraps onto a
      // second line.  Region spans records 261-265.
      {"GG24-4302-00.boo", "3.3.4", "FIGRMFWL01", 43,
       "Figure 12. RMF Workload Activity Report for IMS System Address "
       "Spaces: Control Region, DL/1 SAS, and DBRC",
       {{2, "   |                                                W O R K L O "
            "A D   A C T | V I T Y"},
        {5, "   |       SP5.1.0                 RPT VERSION 5.1.0          "
            "TIME 14.30.11 |"}}},
      {"GG24-4302-00.boo", "3.3.4", "FIGRMFWL02", 53,
       "Figure 13. RMF Workload Activity Report for IMS Dependent Regions",
       {}},
      {"SG24-204.boo", "3.1", "FIGVSEHW1", 19, "Figure 16. VSE IPL Procedure",
       {}},
      // Bullets inside a boxed list render as hosted's degree-sign byte.
      {"SH20-918.boo", "FRONT_1.3", "FIGFIGUNIQ6", 37, "",
       {{5, "   | \xC2\xB0   The GML starter set profile:  DSMPROF4      "
            "                       |"}}},
      // A dashed rule drawn from one-glyph tokens is body, not a frame.
      {"SC34-425.boo", "1.3.4", "FIGDEVPROC", 47, "Figure 13. Development Cycle",
       {{0, "                             _ _ _ _ _ _ _ _ _ _ _ _ _"},
        {3, "                            |       |  MODIFY   |     |"}}},
      // frame=rule figure: the rule lines hosted shows as empty are
      // spacing, the listing is the body.
      {"SC09-138.boo", "1.3.1", "FIGBIO1", 48, "Figure 1. Biorhythm Program",
       {{0, "   /***************************************************************"},
        {2, "    *  Description: Calculates biorhythm based on current         *"},
        {5, ""}}},
      {"SC09-138.boo", "1.3.1", "FIGBIO1SUB", 35,
       "Figure 2. Function for Biorhythm Program", {}},
      {"QSYSNEWG.BOO", "2.1", "FIGFIGUNIQ13", 28, "", {}},
  };
  for (const auto &item : drawn) {
    const std::string label =
        std::string(item.book) + " " + item.topic + " " + item.anchor;
    const auto topic = corpus.topic(item.book, item.topic);
    const auto *figure = find_figure(topic, item.anchor);
    require(figure != nullptr,
            label + ": drawn figure was not admitted\n" +
                geist::detail::format_figure_blocks_ir(topic.figures));
    if (figure == nullptr)
      continue;
    require(figure->body_kind == geist::detail::FigureBodyKindIR::preformatted &&
                figure->target.empty() && figure->span.anchored,
            label + ": drawn figure is not preformatted");
    require(figure->lines.size() == item.line_count,
            label + ": expected " + std::to_string(item.line_count) +
                " body lines, got " + std::to_string(figure->lines.size()));
    for (const auto &[index, text] : item.lines)
      require(index < figure->lines.size() &&
                  figure->lines[index].text == text,
              label + ": body line " + std::to_string(index) + " is '" +
                  (index < figure->lines.size() ? figure->lines[index].text
                                                : std::string("<missing>")) +
                  "'");
    const std::string caption = figure->caption ? figure->caption->text : "";
    require(caption == item.caption, label + ": caption is '" + caption + "'");
    check_conservation(topic, label);
    check_preformatted_lowering(*figure, label);

    // Mutations must be rejected by the verifier.
    const auto &book = corpus.book(item.book);
    auto mutated = topic.figures;
    std::string error;
    for (auto &block : mutated.blocks)
      if (block.anchor == item.anchor)
        block.lines.front().text += "x";
    require(!geist::detail::verify_figure_blocks_ir(
                topic.sources, topic.layout, topic.ownership, topic.selectors,
                book.resource_ids, mutated, &error),
            label + ": mutated body line was accepted");
    mutated = topic.figures;
    for (auto &block : mutated.blocks)
      if (block.anchor == item.anchor)
        block.cells.pop_back();
    require(!geist::detail::verify_figure_blocks_ir(
                topic.sources, topic.layout, topic.ownership, topic.selectors,
                book.resource_ids, mutated, &error),
            label + ": drawn figure with a dropped cell was accepted");
  }

  // Every cell of a drawn figure region is claimed exactly once with a
  // preformatted role: the length byte of each line, the SRFIG opcode, body
  // words, and the SREFIG boundary.
  {
    const auto topic = corpus.topic("FA1PLMM0.boo", "PREFACE.3");
    const auto *figure = find_figure(topic, "FIGFIGUNIQ1");
    std::map<geist::detail::FigureCellRoleIR, std::size_t> roles;
    if (figure != nullptr)
      for (const auto &cell : figure->cells)
        ++roles[cell.role];
    using Role = geist::detail::FigureCellRoleIR;
    require(figure != nullptr && roles[Role::line_prefix] >= 12 &&
                roles[Role::control] > 0 && roles[Role::body_content] > 0 &&
                roles[Role::body_layout] > 0 && roles[Role::boundary] > 0 &&
                roles[Role::placeholder_suppressed] == 0,
            "FA1PLMM0 PREFACE.3: drawn figure cell roles are incomplete");
    // Each body line owns the tokens between its length byte and the next.
    if (figure != nullptr)
      for (std::size_t index = 1; index < figure->lines.size(); ++index) {
        const auto &previous = figure->lines[index - 1];
        const auto &line = figure->lines[index];
        require(line.prefix_token + 1 < line.token_end &&
                    (line.logical_record != previous.logical_record ||
                     previous.token_end <= line.prefix_token),
                "FA1PLMM0 PREFACE.3: body lines overlap");
      }
  }

  // Negative: a drawn figure wrapping an SRTBL is the table family's
  // (SC31-711 2.4.1 FIGTBLUNIQ2 / TBLTBLUNIQ2, IEAC6MST 1.4 FIGDSLIB).
  for (const auto &[book, id] : {std::pair{"SC31-711.boo", "2.4.1"},
                                 std::pair{"IEAC6MST.BOO", "1.4"}}) {
    const auto topic = corpus.topic(book, id);
    require(topic.figures.blocks.empty() &&
                declined_with(topic, "contains a table"),
            std::string(book) + " " + id +
                ": figure wrapping a table was not declined as a table");
  }
  // Negative: a region carrying a menu (SC28-1881-05 1.6 FIGFIGUNIQ71).
  {
    const auto topic = corpus.topic("SC28-1881-05.boo", "1.6");
    require(find_figure(topic, "FIGFIGUNIQ71") == nullptr &&
                declined_with(topic, "contains a menu"),
            "SC28-1881-05 1.6: figure carrying a menu was admitted");
  }

  std::cout << "figure block IR checks passed\n";
  return 0;
}
