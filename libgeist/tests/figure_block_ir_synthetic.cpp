// Typed figure block extraction and lowering over real figures (issue #58).
//
// Positive cases are image-backed figures whose hosted BookServer rendering
// was compared by hand (AnalysisNotes/typed-route-census-2026-08-28.md lists
// the DT values): the anchor name, the picture resource or external path,
// the "PICTURE n" placeholder that the image replaces, and the caption.
// Negative cases prove the extractor fails closed: a picture selector owned
// by a table (GG24-395 3.3.x) is reported as table-owned rather than admitted
// as a figure, and ASCII/CFONT figures without a picture are declined.

#include "geist/boo.hpp"
#include "geist/detail/figure_block_ir.hpp"
#include "geist/detail/figure_document_lowering.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/selector_ir.hpp"
#include "test_failures.hpp"

#include <filesystem>
#include <iostream>
#include <map>
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

  // Negative: CFONT-boxed prose figure (FA1PLMM0 PREFACE.3) and the fixed
  // RMF report figures (GG24-4302-00 3.3.4) carry no picture.
  {
    const auto topic = corpus.topic("FA1PLMM0.boo", "PREFACE.3");
    require(topic.figures.blocks.empty() &&
                declined_with(topic, "no picture selector"),
            "FA1PLMM0 PREFACE.3: ASCII figure was not declined as such");
    const auto reports = corpus.topic("GG24-4302-00.boo", "3.3.4");
    require(reports.figures.blocks.empty() && !reports.figures.declined.empty(),
            "GG24-4302-00 3.3.4: fixed report figure was admitted");
  }

  std::cout << "figure block IR checks passed\n";
  return 0;
}
