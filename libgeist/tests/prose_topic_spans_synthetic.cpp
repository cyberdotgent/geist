// Prose topic composition (issue #58): a prose body as a sequence of prose,
// table and figure spans.  Positive fixtures were compared against hosted
// BookServer (DTs in AnalysisNotes/prose-topic-family-2026-08-28.md):
// SC31-711 4.0 (picture-less SRFIG frame around a box table; hosted serves
// `<a name="FIGTBLUNIQ6">` then `<a name="TBLTBLUNIQ6">`), SC31-711 3.2
// (picture figure between paragraphs), GG24-4302-00 10.2 (captioned table).
// Every mutation of the composed IR must be refused by the verifier, and a
// declined table/figure region must reject the whole topic.
#include "geist/detail/book_topic_catalog_ir.hpp"
#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/prose_topic_document_lowering.hpp"
#include "geist/detail/prose_topic_ir.hpp"
#include "geist/detail/topic_document_lowering.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace geist;
using namespace geist::detail;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "prose_topic_spans_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

struct LoadedBook {
  BooDocument document;
  LogicalDecodeContext context;
  std::optional<BookTopicCatalogIR> catalog;
  std::set<std::string> resource_ids;

  explicit LoadedBook(const std::filesystem::path& path)
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
    std::string error;
    catalog = build_book_topic_catalog_ir(document.topics(),
                                          document.table_of_contents(), &error);
    for (const auto& resource : document.resources())
      resource_ids.insert(ascii_lower(resource.id));
  }
};

LoadedBook& book(const std::string& file) {
  static std::map<std::string, std::unique_ptr<LoadedBook>> loaded;
  if (loaded.count(file) == 0)
    loaded[file] = std::make_unique<LoadedBook>(
        std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / file);
  return *loaded[file];
}

struct Extracted {
  std::vector<DecodedLogicalRecordSource> sources;
  LayoutIR layout;
  std::optional<VerifiedOwnershipIR> ownership;
  TopicIdentityIR identity;
  const BookTopicCatalogIR* catalog = nullptr;
  const std::set<std::string>* resource_ids = nullptr;
  std::optional<ProseTopicIR> prose;
  std::string error;

  bool verify(const ProseTopicIR& candidate, std::string* error_out) const {
    return verify_prose_topic_ir(sources, layout, *ownership, identity.title,
                                 catalog, candidate, error_out, resource_ids);
  }
};

Extracted extract(const std::string& file, const std::string& id) {
  auto& loaded = book(file);
  Extracted result;
  const auto info = std::find_if(
      loaded.document.topics().begin(), loaded.document.topics().end(),
      [&](const auto& candidate) { return candidate.id == id; });
  const auto* entry = loaded.document.find_toc_entry(id);
  require(info != loaded.document.topics().end() && entry != nullptr,
          "missing fixture topic " + file + " " + id);
  if (info == loaded.document.topics().end() || entry == nullptr) return result;
  result.sources = decode_logical_record_sources(
      loaded.context, info->start_logical_record, info->end_logical_record);
  result.layout = extract_layout_ir(result.sources);
  result.ownership =
      build_verified_ownership_ir(result.sources, result.layout, &result.error);
  if (!result.ownership) return result;
  result.identity.id = entry->id;
  result.identity.title = entry->title;
  result.identity.heading_level = info->heading_level;
  result.identity.topic_number = info->topic_number;
  result.identity.start_logical_record = info->start_logical_record;
  result.identity.end_logical_record = info->end_logical_record;
  result.catalog = loaded.catalog ? &*loaded.catalog : nullptr;
  result.resource_ids = &loaded.resource_ids;
  result.prose = extract_prose_topic_ir(result.sources, result.layout,
                                        *result.ownership, entry->title,
                                        result.catalog, &result.error,
                                        result.resource_ids);
  return result;
}

std::size_t count_role(const ProseTopicIR& prose, ProseTokenRoleIR role) {
  return static_cast<std::size_t>(std::count_if(
      prose.ledger.begin(), prose.ledger.end(),
      [&](const auto& entry) { return entry.role == role; }));
}

// Extraction, conservation, lowering, document verification and the
// dispatcher route; returns the Markdown and keeps the extraction.
std::string admit(const std::string& file, const std::string& id,
                  Extracted& kept) {
  kept = extract(file, id);
  const auto label = file + " " + id;
  require(kept.prose.has_value(), label + " rejected: " + kept.error);
  if (!kept.prose) return {};
  const auto& prose = *kept.prose;
  std::string error;
  require(kept.verify(prose, &error), label + " verification failed: " + error);
  // Conservation: every token has one role; span tokens name their span,
  // every span owns tokens of its kind only, and no span is empty.
  std::vector<std::size_t> owned(prose.spans.size(), 0);
  for (const auto& entry : prose.ledger) {
    require(entry.role != ProseTokenRoleIR::unassigned,
            label + " ledger has an unassigned token");
    const auto span_role = entry.role == ProseTokenRoleIR::table ||
                           entry.role == ProseTokenRoleIR::figure;
    require(span_role == (entry.span != static_cast<std::size_t>(-1)),
            label + " span ownership disagrees with the token role");
    if (span_role && entry.span < prose.spans.size()) {
      ++owned[entry.span];
      require((prose.spans[entry.span].kind == ProseSpanKindIR::table) ==
                  (entry.role == ProseTokenRoleIR::table),
              label + " span kind disagrees with the token role");
    }
  }
  for (const auto count : owned)
    require(count != 0, label + " has a span owning no token");
  // Spans are in source order (positions never decrease).
  for (std::size_t index = 1; index < prose.spans.size(); ++index)
    require(prose.spans[index - 1].position <= prose.spans[index].position,
            label + " spans are out of order");
  auto document = lower_prose_topic_to_document_ir(kept.identity, prose, &error);
  require(document.has_value(), label + " lowering failed: " + error);
  if (!document) return {};
  require(verify_prose_topic_document_ir(prose, *document, &error),
          label + " document verification failed: " + error);
  std::string rejection;
  const auto routed = try_lower_topic_to_document_ir(
      kept.identity, kept.sources, kept.catalog, &rejection, nullptr,
      kept.resource_ids);
  require(routed.has_value(), label + " dispatcher declined: " + rejection);
  return routed ? render_document_markdown(*routed) : std::string{};
}

void reject(const std::string& file, const std::string& id,
            const std::string& reason) {
  const auto extracted = extract(file, id);
  const auto label = file + " " + id;
  require(!extracted.prose.has_value(), label + " was admitted");
  require(contains(extracted.error, reason),
          label + " rejected for '" + extracted.error + "', expected '" +
              reason + "'");
}

template <typename Node>
std::size_t count_nodes(const DocumentIR& document) {
  return static_cast<std::size_t>(std::count_if(
      document.blocks.begin(), document.blocks.end(), [](const auto& block) {
        return std::holds_alternative<Node>(block.node);
      }));
}

void mutations(const Extracted& kept, const std::string& label) {
  const auto& prose = *kept.prose;
  std::string error;
  const auto refused = [&](const ProseTopicIR& mutated, const char* what) {
    require(!kept.verify(mutated, &error),
            label + ": " + what + " was accepted by the verifier");
  };
  // Dropped table/figure span: its tokens keep pointing at it.
  {
    auto mutated = prose;
    mutated.spans.pop_back();
    refused(mutated, "dropped span");
  }
  // Dropped table block.
  if (!prose.tables.blocks.empty()) {
    auto mutated = prose;
    mutated.tables.blocks.pop_back();
    refused(mutated, "dropped table block");
  }
  // Span moved to another position.
  {
    auto mutated = prose;
    auto& span = mutated.spans.front();
    span.position = span.position == 0 ? prose.blocks.size() : 0;
    refused(mutated, "moved span");
    // The moved span also lowers to a different document.
    const auto original =
        lower_prose_topic_to_document_ir(kept.identity, prose, &error);
    const auto moved =
        lower_prose_topic_to_document_ir(kept.identity, mutated, &error);
    require(original && moved &&
                format_document_ir(*original) != format_document_ir(*moved),
            label + ": moved span lowered to the same document");
    if (original)
      require(!verify_prose_topic_document_ir(mutated, *original, &error),
              label + ": document of the moved span was accepted");
  }
  // Figure caption edited.
  if (!prose.figures.blocks.empty() && prose.figures.blocks.front().caption) {
    auto mutated = prose;
    mutated.figures.blocks.front().caption->text += " edited";
    refused(mutated, "edited figure caption");
  }
  // Table cell edited.
  if (!prose.tables.blocks.empty() &&
      !prose.tables.blocks.front().body.empty() &&
      !prose.tables.blocks.front().body.front().cells.empty() &&
      !prose.tables.blocks.front().body.front().cells.front().lines.empty()) {
    auto mutated = prose;
    mutated.tables.blocks.front().body.front().cells.front().lines.front().text +=
        " edited";
    refused(mutated, "edited table cell");
  }
  // A prose paragraph claiming a token inside a table/figure span.
  {
    auto mutated = prose;
    const auto entry = std::find_if(
        mutated.ledger.begin(), mutated.ledger.end(), [](const auto& e) {
          return e.role == ProseTokenRoleIR::table ||
                 e.role == ProseTokenRoleIR::figure;
        });
    require(entry != mutated.ledger.end(), label + ": no span token");
    if (entry != mutated.ledger.end() && !mutated.blocks.empty()) {
      entry->role = ProseTokenRoleIR::text;
      entry->block = 0;
      entry->inline_index = 0;
      entry->span = static_cast<std::size_t>(-1);
      refused(mutated, "prose text inside a span");
    }
  }
  // A duplicated prose block inside the span's position.
  if (!prose.blocks.empty()) {
    auto mutated = prose;
    mutated.blocks.insert(mutated.blocks.begin() + static_cast<std::ptrdiff_t>(
                                                       prose.spans.front().position),
                          prose.blocks.front());
    refused(mutated, "prose paragraph inserted at a span");
  }
}

} // namespace

int main() {
  // Picture-less SRFIG frame around a two-column box table, between the
  // second paragraph and the trailing menu.
  {
    Extracted kept;
    const auto markdown = admit("SC31-711.boo", "4.0", kept);
    if (kept.prose) {
      const auto& prose = *kept.prose;
      require(prose.spans.size() == 1 &&
                  prose.spans.front().kind == ProseSpanKindIR::table &&
                  prose.tables.blocks.size() == 1 &&
                  prose.tables.declined.empty(),
              "4.0 has one table span");
      require(prose.blocks.size() == 2 &&
                  prose.spans.front().position == 2,
              "4.0 table follows both paragraphs");
      require(std::any_of(prose.anchors.begin(), prose.anchors.end(),
                          [](const auto& anchor) {
                            return anchor.id == "FIGTBLUNIQ6" &&
                                   anchor.position == 2;
                          }),
              "4.0 frame anchor FIGTBLUNIQ6 precedes the table");
      require(prose.spans.front().anchors_before == 1,
              "4.0 table follows the frame anchor");
      require(count_role(prose, ProseTokenRoleIR::table) > 0 &&
                  count_role(prose, ProseTokenRoleIR::figure) == 0,
              "4.0 tokens are owned by the table span");
      require(contains(markdown, "<a id=\"FIGTBLUNIQ6\"></a>\n\n<a id=\"TBLTBLUNIQ6\"></a>"),
              "4.0 anchors keep the hosted order: " + markdown);
      // CSELECT cells link line by line, as hosted and legacy do.
      require(contains(markdown, "| LNM OS/2 agent traps | [\"LNM OS/2 Agent "
                                 "Application Traps\" in](<#HDRLMATRP>)<br>"
                                 "[topic 4\\.1](<#HDRLMATRP>) |"),
              "4.0 table row links: " + markdown);
      require(prose.table_links.size() == 5,
              "4.0 has five table links");
      require(contains(markdown, "they originate\\.\n\nWhen the LNM"),
              "4.0 paragraphs before the table: " + markdown);
      require(contains(markdown, "topic 4\\.4](<#HDRFDDITRP>) |\n\nSubtopics:"),
              "4.0 menu follows the table: " + markdown);
      mutations(kept, "SC31-711 4.0");
    }
  }
  // Picture figure between paragraphs, resolved against the resource
  // catalog; the SREFIG segment carries the following prose as payload.
  {
    Extracted kept;
    const auto markdown = admit("SC31-711.boo", "3.2", kept);
    if (kept.prose) {
      const auto& prose = *kept.prose;
      require(prose.spans.size() == 1 &&
                  prose.spans.front().kind == ProseSpanKindIR::figure &&
                  prose.figures.blocks.size() == 1 &&
                  prose.figures.blocks.front().anchor == "FIGTRPFLOW",
              "3.2 has one figure span");
      require(prose.spans.front().position > 0 &&
                  prose.spans.front().position < prose.blocks.size(),
              "3.2 figure sits between prose blocks");
      require(contains(markdown, "<a id=\"FIGTRPFLOW\"></a>\n\n![Figure 1\\. Trap Flow"),
              "3.2 figure anchor and image: " + markdown);
      require(contains(markdown, "and back again\\.\n\n<a id=\"FIGTRPFLOW\">"),
              "3.2 paragraph before the figure: " + markdown);
      require(contains(markdown, "NetView/6000*\n\nWhen LNM for AIX is installed, the **addtrap**"),
              "3.2 prose after the figure: " + markdown);
      mutations(kept, "SC31-711 3.2");
    }
  }
  // Captioned box table with a bold header row.
  {
    Extracted kept;
    const auto markdown = admit("GG24-4302-00.boo", "10.2", kept);
    if (kept.prose) {
      const auto& prose = *kept.prose;
      require(prose.spans.size() == 1 && prose.tables.blocks.size() == 1 &&
                  prose.tables.blocks.front().caption.has_value(),
              "10.2 has one captioned table span");
      require(contains(markdown, "<a id=\"TBLDBCTL51\"></a>\n\nTable 15\\. DBCTL 5\\.1 Overview\n\n|  | TM | DBCTL |\n| --- | --- | --- |"),
              "10.2 caption and header: " + markdown);
      require(contains(markdown, "[Table 15](<#TBLDBCTL51>)"),
              "10.2 cross reference targets the table anchor: " + markdown);
      mutations(kept, "GG24-4302-00 10.2");
    }
  }
  {
    // A `SI` subject-index line inside a table envelope is hidden and is
    // claimed by the table span.  SC24-5527-02 4.1.1 record 380 opens the
    // `XSESDSK` envelope with `SI VMSES/E, service disks`; hosted
    // (DT 19921218151459) serves the caption line straight after the top
    // rule and shows no `SI` byte.
    Extracted kept;
    const auto markdown = admit("SC24-5527-02.boo", "4.1.1", kept);
    require(!contains(markdown, "SI VMSES") &&
                !contains(markdown, "service disks"),
            "4.1.1 printed the subject-index line of its table envelope");
    require(contains(markdown, "Table  4\\-1\\. Service Disks for VMSES/E"),
            "4.1.1 lost its table caption: " + markdown);
    if (kept.prose) {
      const auto& terms = kept.prose->index_terms;
      require(std::count_if(terms.begin(), terms.end(),
                            [](const auto& term) {
                              return term.term == "SI VMSES/E, service disks";
                            }) == 1,
              "4.1.1 lost the envelope's subject-index term");
    }
  }
  {
    // A preformatted envelope may carry anchor and `LNK` selectors: their
    // opcode, operands and `<...>` alternatives are one hidden display line
    // each, so the region still reproduces exactly.
    Extracted kept;
    const auto markdown = admit("SC24-5527-02.boo", "ROADMAP", kept);
    require(!contains(markdown, "cselect") && !contains(markdown, "<BOOK>") &&
                !contains(markdown, "<SC24-5444>"),
            "ROADMAP printed a selector operand: " + markdown);
  }
  {
    // A `LNK` selector covering a whole table cell line lowers to the same
    // external cross reference the prose inline carries: hosted
    // SC24-5527-02 3.9.4.4 (DT 19921218151459) serves the `TBLUNIQ156` cell
    // as `<a href="../../DOCNUM/SC24-5521/CCONTENTS?DocnumLevel=ANY">`.
    Extracted kept;
    const auto markdown = admit("SC24-5527-02.boo", "3.9.4.4", kept);
    require(contains(markdown, "(<DOCNUM/SC24-5521/CCONTENTS>)"),
            "3.9.4.4 lost its cross-book table cell link: " + markdown);
    if (kept.prose) {
      const auto& links = kept.prose->table_links;
      require(std::any_of(links.begin(), links.end(),
                          [](const auto& link) {
                            return link.target_kind ==
                                       CrossReferenceTargetKindIR::external &&
                                   link.target ==
                                       "DOCNUM/SC24-5521/CCONTENTS";
                          }),
              "3.9.4.4 table link is not typed as an external reference");
    }
  }
  // Fail-closed classes: a declined table envelope or figure region rejects
  // the whole topic, and so does a picture without a resource catalog.
  // ACPZMST1 4.3 `TBLUNIQ39` and IEAC6MST 4.3.4.1 `TBLUNIQ10` used to stand
  // here; both envelopes now compose, so the class is pinned on one that
  // still declines both models.
  reject("IEAC6MST.BOO", "7.9",
         "table envelope 'CLISTS' declined: visible source between table "
         "lines");
  // Nested figure regions: DREICMST 1.2.1 records 79-84 wrap
  // `SRFIGXXX`/`SRTBLXXX` in an outer `SRFIGLOGPROC`, closed by two
  // `SREFIG`.  Hosted DT 19911219125856 serves
  // `<a name="FIGLOGPROC">   split=yes.</a>` and then
  // `<a name="FIGXXX"><a name="TBLXXX">` on the table's top rule, so both
  // anchors, the lead line and the caption are real; the region used to be
  // declined as unterminated at the inner opener.
  {
    Extracted kept;
    const auto markdown = admit("DREICMST.boo", "1.2.1", kept);
    require(contains(markdown, "split = yes"),
            "DREICMST 1.2.1 lost the outer figure's lead line: " + markdown);
    require(contains(markdown, "Figure 5"),
            "DREICMST 1.2.1 lost the outer figure's caption: " + markdown);
    require(contains(markdown, "id=\"FIGLOGPROC\"") &&
                contains(markdown, "id=\"TBLXXX\""),
            "DREICMST 1.2.1 lost a nested anchor: " + markdown);
  }
  {
    const auto extracted = extract("SC31-711.boo", "3.2");
    std::string error;
    const auto without = extract_prose_topic_ir(
        extracted.sources, extracted.layout, *extracted.ownership,
        extracted.identity.title, extracted.catalog, &error, nullptr);
    require(!without.has_value() &&
                contains(error, "picture resource 1 is not in the resource catalog"),
            "3.2 without a resource catalog: " + error);
  }
  geist_test::exit_with_failures();
  return 0;
}
