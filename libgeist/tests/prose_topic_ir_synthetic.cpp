// Ordinary prose topic family (issue #58): typed extraction, conservation,
// lowering and fail-closed classes over real fixtures.
//
// Positive fixtures were compared word for word against hosted BookServer
// (DT in AnalysisNotes): SC31-711 2.2.1/2.2.3/2.3.2, QSYSINFO 1.1.5,
// SH20-918 1.4.1/1.1, FA1PLMM0 CHANGES.1.  Negative fixtures cover one topic per
// fail-closed class; each must reject with the named reason so a later
// widening of the model is a deliberate change here as well.
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
    std::cerr << "prose_topic_ir_synthetic: " << message << '\n';
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

std::map<std::string, std::unique_ptr<LoadedBook>>& books() {
  static std::map<std::string, std::unique_ptr<LoadedBook>> loaded;
  return loaded;
}

LoadedBook& book(const std::string& file) {
  auto& loaded = books();
  if (loaded.count(file) == 0)
    loaded[file] = std::make_unique<LoadedBook>(
        std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / file);
  return *loaded[file];
}

struct Extracted {
  std::vector<DecodedLogicalRecordSource> sources;
  LayoutIR layout;
  OwnershipIR ownership;
  TopicIdentityIR identity;
  const BookTopicCatalogIR* catalog = nullptr;
  const std::set<std::string>* resource_ids = nullptr;
  std::optional<ProseTopicIR> prose;
  std::string error;
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
  result.ownership = build_ownership_ir(result.sources, result.layout);
  result.identity.id = entry->id;
  result.identity.title = entry->title;
  result.identity.heading_level = info->heading_level;
  result.identity.topic_number = info->topic_number;
  result.identity.start_logical_record = info->start_logical_record;
  result.identity.end_logical_record = info->end_logical_record;
  result.catalog = loaded.catalog ? &*loaded.catalog : nullptr;
  result.resource_ids = &loaded.resource_ids;
  result.prose = extract_prose_topic_ir(result.sources, result.layout,
                                        result.ownership, entry->title,
                                        result.catalog, &result.error,
                                        result.resource_ids);
  return result;
}

// Full positive check: extraction, conservation verifier, lowering, document
// verifier, and the dispatcher route.  Returns the rendered Markdown.
std::string admit(const std::string& file, const std::string& id,
                  Extracted* keep = nullptr) {
  auto extracted = extract(file, id);
  const auto label = file + " " + id;
  require(extracted.prose.has_value(), label + " rejected: " + extracted.error);
  if (!extracted.prose) return {};
  std::string error;
  require(verify_prose_topic_ir(extracted.sources, extracted.layout,
                                extracted.ownership, extracted.identity.title,
                                extracted.catalog, *extracted.prose, &error,
                                extracted.resource_ids),
          label + " verification failed: " + error);
  for (const auto& entry : extracted.prose->ledger)
    require(entry.role != ProseTokenRoleIR::unassigned,
            label + " ledger has an unassigned token");
  auto document = lower_prose_topic_to_document_ir(extracted.identity,
                                                   *extracted.prose, &error);
  require(document.has_value(), label + " lowering failed: " + error);
  if (!document) return {};
  require(verify_prose_topic_document_ir(*extracted.prose, *document, &error),
          label + " document verification failed: " + error);
  std::string rejection;
  const auto routed = try_lower_topic_to_document_ir(
      extracted.identity, extracted.sources, extracted.catalog, &rejection,
      nullptr, extracted.resource_ids);
  require(routed.has_value(), label + " dispatcher declined: " + rejection);
  if (routed) {
    const auto prose_block = std::find_if(
        routed->blocks.begin(), routed->blocks.end(), [](const auto& block) {
          return block.origin.detail.rfind("prose ", 0) == 0;
        });
    require(prose_block != routed->blocks.end(),
            label + " dispatcher used a different family");
  }
  if (keep != nullptr) *keep = std::move(extracted);
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
  std::string rejection;
  const auto routed = try_lower_topic_to_document_ir(
      extracted.identity, extracted.sources, extracted.catalog, &rejection);
  require(!routed.has_value() || rejection.empty(),
          label + " reached the typed route through another family");
}

std::size_t count_blocks(const ProseTopicIR& prose, ProseBlockKindIR kind) {
  return static_cast<std::size_t>(std::count_if(
      prose.blocks.begin(), prose.blocks.end(),
      [&](const auto& block) { return block.kind == kind; }));
}

void positive_fixtures() {
  // One wrapped paragraph; the SI term is suppressed and the two-run marker
  // word `for` (SC31-711 LR37 token 82) is text, as hosted renders it.
  {
    Extracted kept;
    const auto markdown = admit("SC31-711.boo", "2.2.1", &kept);
    require(contains(markdown, "look at the nettl log for information"),
            "2.2.1 lost the fill-run marker word");
    require(!contains(markdown, "agent discovery problems"),
            "2.2.1 rendered the SI index term");
    if (kept.prose) {
      require(kept.prose->index_terms.size() == 1 &&
                  kept.prose->index_terms.front().term ==
                      "agent discovery problems",
              "2.2.1 index term provenance is wrong");
      require(kept.prose->blocks.size() == 1, "2.2.1 block count");
    }
  }
  // Paragraph, three bullet items with HP2 emphasis, closing paragraph with
  // a CIT citation; the standalone `/` glyph before a fill/origin pair is a
  // marker (hosted shows `Getting` / `Started`).
  {
    Extracted kept;
    const auto markdown = admit("SC31-711.boo", "2.2.3", &kept);
    require(contains(markdown, "- The **ovwdb** and **gtmd** processes are "
                               "running\\."),
            "2.2.3 first list item");
    // Adjacent same-style CIT words merge into one emphasis span.
    require(contains(markdown,
                     "*Getting Started with LAN Network Manager for AIX*\\."),
            "2.2.3 citation");
    require(!contains(markdown, "Getting /"), "2.2.3 kept the `/` marker");
    if (kept.prose) {
      require(count_blocks(*kept.prose, ProseBlockKindIR::list_item) == 3,
              "2.2.3 list item count");
      require(count_blocks(*kept.prose, ProseBlockKindIR::paragraph) == 2,
              "2.2.3 paragraph count");
    }
  }
  // Anchor, checklist paragraphs, links split across rows, XPH code spans,
  // nested bullet rows, and a trailing CMENU validated through the glued
  // header title.
  {
    Extracted kept;
    const auto markdown = admit("SC31-711.boo", "2.3.2", &kept);
    require(contains(markdown, "<a id=\"HDRSTRPRB\"></a>"), "2.3.2 anchor");
    require(contains(markdown,
                     "[\"Gathering Problem Information\" in topic 2\\.1]"
                     "(<#HDRHOWTSL>)"),
            "2.3.2 cross-reference");
    require(contains(markdown, "`ps -ef | grep lnm`"), "2.3.2 code spans");
    require(contains(markdown, "- lnmtrmon is running and is a child"),
            "2.3.2 bullet item");
    require(contains(markdown, "Subtopics:\n\n- [2\\.3\\.2\\.1 Permanent "
                               "Hourglass on SNMP Token\\-Ring Windows]"
                               "(<#2.3.2.1>)"),
            "2.3.2 trailing menu");
    require(!contains(markdown, "trapd,"), "2.3.2 kept the `,` marker slot");
    if (kept.prose) require(kept.prose->menu_items.size() == 1, "2.3.2 menu");
  }
  // Two-run marker words `of` restored, a selector split over two rows, and
  // a Note paragraph whose label is an HP2 span.
  {
    const auto markdown = admit("QSYSINFO.BOO", "1.1.5");
    require(contains(markdown, "basic concepts of the AS/400 system"),
            "1.1.5 lost a fill-run marker word");
    require(contains(markdown, "[\"Other AS/400](<#HDRRELI>) [Information "
                               "from IBM\" in topic 2\\.2](<#HDRRELI>)"),
            "1.1.5 split selector");
    require(contains(markdown, "**Note:** The videotapes"), "1.1.5 note");
  }
  // Bullet items with SI terms between them, CFONT emphasis and a trailing
  // menu whose one-byte final label word (`GML`) is title text.
  admit("SH20-918.boo", "1.4.1");
  {
    Extracted kept;
    admit("SH20-918.boo", "1.4", &kept);
    if (kept.prose)
      require(std::any_of(kept.prose->menu_items.begin(),
                          kept.prose->menu_items.end(),
                          [](const auto& item) {
                            return item.label ==
                                   "Advantages of Using SCRIPT/VS and GML";
                          }),
              "SH20-918 1.4 menu label lost its one-byte final word");
  }
  admit("SH20-918.boo", "1.1");
  // Bare spacing tokens inside the title are attach glue (`X'48'`).
  {
    Extracted kept;
    admit("SC24-5520-00.boo", "1.1.20", &kept);
    if (kept.prose)
      require(kept.prose->title == "Diagnose Code X'48'--Second Level SVC 76",
              "1.1.20 title");
  }
  // Title glued into the csourcefn control (`csourcefn X ? ST? title`).
  {
    Extracted kept;
    admit("ACPZMST1.boo", "2.2", &kept);
    if (kept.prose)
      require(kept.prose->title == "Querying VM PWSCS", "ACPZMST1 2.2 title");
  }
  // Anchor after the trailing menu.
  {
    Extracted kept;
    const auto markdown = admit("ACPZMST1.boo", "1.1", &kept);
    require(contains(markdown, "(<#1.1.3>)\n\n<a id=\"SPTSNA1\"></a>"),
            "ACPZMST1 1.1 anchor after menu");
  }
  // Glued one-byte marker word after the title (`Messages` + `access`).
  {
    Extracted kept;
    admit("FA1PLMM0.boo", "I.6.1", &kept);
    if (kept.prose)
      require(kept.prose->title == "Action-Flag Messages", "I.6.1 title");
  }
  admit("FA1PLMM0.boo", "CHANGES.1");
  // Structured subject-index lines: `SI ??3HI1?0?<title>?` and
  // `SI ??4XMP@?0?<term>?  <term>`.  The whole display line is hidden;
  // hosted QSYSINFO 2.1.1 (DT 19910524120827) and SC09-138 2.1.1.2
  // (DT 19910321130500) display none of its fields.
  {
    Extracted kept;
    const auto markdown = admit("QSYSINFO.BOO", "2.1.1", &kept);
    require(contains(markdown, "*Publication Description*: The"),
            "QSYSINFO 2.1.1 body");
    require(!contains(markdown, "3HI1"),
            "QSYSINFO 2.1.1 rendered a structured index field");
    if (kept.prose) {
      require(kept.prose->index_terms.size() == 4,
              "QSYSINFO 2.1.1 index term count");
      const auto structured = std::count_if(
          kept.prose->index_terms.begin(), kept.prose->index_terms.end(),
          [](const auto& term) { return term.structured; });
      require(structured == 2, "QSYSINFO 2.1.1 structured index terms");
      require(!kept.prose->index_terms.front().structured &&
                  kept.prose->index_terms.front().term ==
                      "planning, physical",
              "QSYSINFO 2.1.1 plain index term");
    }
  }
  {
    const auto markdown = admit("SC09-138.boo", "2.1.1.2");
    require(contains(markdown, "DEFAULT:"), "SC09-138 2.1.1.2 body");
    require(!contains(markdown, "4XMP"),
            "SC09-138 2.1.1.2 rendered a structured index field");
    require(!contains(markdown, "compile-time option"),
            "SC09-138 2.1.1.2 rendered a hidden index term");
  }
}

void mutation_fixtures() {
  auto extracted = extract("SC31-711.boo", "2.2.3");
  require(extracted.prose.has_value(), "mutation fixture rejected");
  if (!extracted.prose) return;
  const auto& sources = extracted.sources;
  const auto verify = [&](const ProseTopicIR& mutated) {
    std::string error;
    return verify_prose_topic_ir(sources, extracted.layout, extracted.ownership,
                                 extracted.identity.title, extracted.catalog,
                                 mutated, &error);
  };
  {
    auto mutated = *extracted.prose;
    auto text = std::find_if(mutated.ledger.begin(), mutated.ledger.end(),
                             [](const auto& entry) {
                               return entry.role == ProseTokenRoleIR::text;
                             });
    text->role = ProseTokenRoleIR::gap;
    require(!verify(mutated), "reassigned text token passed verification");
  }
  {
    auto mutated = *extracted.prose;
    mutated.blocks.front().inlines.front().text += " extra";
    require(!verify(mutated), "altered inline text passed verification");
  }
  {
    auto mutated = *extracted.prose;
    mutated.blocks.erase(mutated.blocks.begin());
    require(!verify(mutated), "dropped block passed verification");
  }
  {
    auto mutated = *extracted.prose;
    mutated.index_terms.clear();
    require(!verify(mutated), "dropped index term passed verification");
  }
  {
    std::string error;
    auto document = lower_prose_topic_to_document_ir(extracted.identity,
                                                     *extracted.prose, &error);
    require(document.has_value(), "mutation lowering failed: " + error);
    if (document) {
      auto& paragraph =
          std::get<ParagraphBlockIR>(document->blocks[1].node);
      std::get<TextInlineIR>(paragraph.content.front().node).text = "x";
      require(!verify_prose_topic_document_ir(*extracted.prose, *document,
                                              &error),
              "altered document passed verification");
    }
  }
  {
    std::string error;
    const auto wrong = extract_prose_topic_ir(
        sources, extracted.layout, extracted.ownership, "Another Title",
        extracted.catalog, &error);
    require(!wrong.has_value() && contains(error, "does not match"),
            "title mismatch was admitted");
  }
}

void negative_fixtures() {
  // Tables and figures compose (tests/prose_topic_spans_synthetic.cpp); a
  // declined envelope still rejects the whole topic.
  reject("ACPZMST1.boo", "FRONT_1.2",
         "table envelope 'TBLUNIQ1' declined: cell text has an unaligned gap: 'AIX/6000                 AIXwindows'");
  reject("ITPPIBOK.BOO", "1.3.7", "picture or external link");
  reject("packet.boo", "1.1", "body control cz is outside the prose model");
  reject("SC24-546.boo", "3.1", "metadata controls are incomplete");
  reject("PRG1SORT.boo", "1.1.5.1", "control-like word 'SRCFILE'");
  reject("ACPZMST1.boo", "COVER", "is not an h1-h6 prose heading");
  // Two-column definition-list example (`TERMS  DESCRIPTIONS` header): the
  // implied row break misaligns the header spans, so the form fails closed.
  reject("SH20-918.boo", "2.2.2", "exceeds the display line");
  // Plural CFONT header over repeated row controls: the legacy route draws
  // this NetView directory list as a table; prose must not flatten it.
  reject("SC31-711.boo", "1.2", "implicit two-column grid");
  // A trailing menu needs the book catalog to validate its targets.
  {
    auto extracted = extract("SC31-711.boo", "2.3.2");
    std::string error;
    const auto without = extract_prose_topic_ir(
        extracted.sources, extracted.layout, extracted.ownership,
        extracted.identity.title, nullptr, &error);
    require(!without.has_value() && contains(error, "book topic catalog"),
            "menu admitted without a catalog");
  }
}

} // namespace

int main() {
  positive_fixtures();
  mutation_fixtures();
  negative_fixtures();
  geist_test::exit_with_failures();
  std::cout << "prose_topic_ir_synthetic: ok\n";
  return 0;
}
