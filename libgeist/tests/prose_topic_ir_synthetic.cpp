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
  // Drawn box regions: a screen box (OFCUSEOV 1.10 DT 19900805103816) and a
  // labelled note box (QSYSNEWG 1.0 DT 19910524085706).  Hosted BookServer
  // prints the region's display lines verbatim inside its <pre>, so the rows
  // become one preformatted block; the legacy route dropped the outline and
  // reflowed the rows into the body paragraph.
  {
    Extracted kept;
    const auto markdown = admit("QSYSNEWG.BOO", "1.0", &kept);
    require(contains(markdown, "```\n ___ In a Hurry? ____"),
            "QSYSNEWG 1.0 box top rule");
    require(contains(markdown,
                     "| This chapter contains background information about "
                     "computers and       |"),
            "QSYSNEWG 1.0 box body row");
    require(contains(markdown, "|______"), "QSYSNEWG 1.0 box bottom rule");
    if (kept.prose) {
      require(count_blocks(*kept.prose, ProseBlockKindIR::preformatted) == 1,
              "QSYSNEWG 1.0 preformatted block count");
      require(count_blocks(*kept.prose, ProseBlockKindIR::paragraph) == 0,
              "QSYSNEWG 1.0 has no reflowed paragraph");
    }
  }
  {
    Extracted kept;
    const auto markdown = admit("OFCUSEOV.BOO", "1.18.2", &kept);
    require(contains(markdown,
                     "|   Type information, press Enter to schedule."),
            "OFCUSEOV 1.18.2 box body row");
    if (kept.prose)
      require(count_blocks(*kept.prose, ProseBlockKindIR::preformatted) == 1,
              "OFCUSEOV 1.18.2 preformatted block count");
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

// Front matter and envelope variants (issue #58).  Every fixture below was
// checked against hosted BookServer at the DT recorded beside it.
void front_matter_fixtures() {
  // `CHDLEVEL :<form>`: front-matter topics name their heading form instead
  // of a level.  Hosted serves every one of them as `<H1>`.
  const struct {
    const char* file;
    const char* id;
    const char* form;
  } forms[] = {
      {"GG24-4302-00.boo", "EDITION", "vnotice"},      // DT 19950308184737
      {"SC31-711.boo", "PREFACE", "preface"},          // DT 19941010174546
      {"SC24-5520-00.boo", "NOTICES", "notices"},      // absent from catalog
      {"SH20-918.boo", "GLOSSARY", "glossary"},        // DT 19910520154851
      {"SH20-918.boo", "TITLE", "title"},              // DT 19910520154851
      {"ITPPIBOK.BOO", "BIBLIOGRAPHY", "bibliog"},     // DT 19910628074854
      {"SC33-033.boo", "ABSTRACT", "abstract"},        // DT 19930422134757
      {"GG24-395.boo", "ABBREVIATIONS", "abbrev"},     // DT 19941215160749
      {"SC24-546.boo", "CHANGES", "soa"},              // DT 19940323131240
  };
  for (const auto& entry : forms) {
    Extracted extracted;
    const auto markdown = admit(entry.file, entry.id, &extracted);
    const std::string label =
        std::string(entry.file) + " " + entry.id + " (" + entry.form + ")";
    if (!extracted.prose) continue;
    require(extracted.prose->heading_form == entry.form,
            label + " kept heading form '" + extracted.prose->heading_form +
                "'");
    require(extracted.prose->heading_level == "h1",
            label + " is not a level-1 heading");
    require(contains(markdown, "\n# ") || markdown.rfind("# ", 0) == 0,
            label + " did not render a level-1 heading");
  }

  // Envelope anchor variant: `SRLEN <text>` between `CSUMMARY` and
  // `CHDLEVEL`.  Hosted serves the whole control without `SR` as the anchor
  // name and prints none of its payload: SC33-033 4.99 record 594
  // `SRLEN CHYQST` is `<a name="LEN CHYQST">` at DT 19930422134757.
  {
    Extracted extracted;
    admit("SC33-033.boo", "4.99", &extracted);
    if (extracted.prose) {
      const auto& anchors = extracted.prose->anchors;
      require(std::any_of(anchors.begin(), anchors.end(),
                          [](const auto& anchor) {
                            return anchor.id.rfind("LEN ", 0) == 0;
                          }),
              "SC33-033 4.99 lost its LEN envelope anchor");
    }
  }
  // `SRLEN` with no payload names the bare anchor `LEN` (SC34-425 2.5
  // record 1478, hosted `<a name="LEN"><a name="HDRADVTP">`, DT
  // 19921112160049).
  {
    Extracted extracted;
    admit("SC34-425.boo", "2.5", &extracted);
    if (extracted.prose) {
      const auto& anchors = extracted.prose->anchors;
      require(std::any_of(anchors.begin(), anchors.end(),
                          [](const auto& anchor) { return anchor.id == "LEN"; }),
              "SC34-425 2.5 lost its bare LEN envelope anchor");
    }
  }

  // A body `SR<id>` anchor keeps its own id and its payload is display text
  // hosted wraps in the anchor: ACPZMST1 record 155
  // `SRSPTSETDC A domain controller handles ...` is served as
  // `<a name="SPTSETDC">   A domain controller handles ...</a>`
  // (DT 19920319123146).
  {
    Extracted extracted;
    const auto markdown = admit("ACPZMST1.boo", "3.1", &extracted);
    require(contains(markdown, "<a id=\"SPTSETDC\"></a>"),
            "ACPZMST1 3.1 lost its body anchor");
    require(contains(markdown, "A domain controller handles communications"),
            "ACPZMST1 3.1 lost the anchor's display payload");
  }

  // `c.cp` pagination: the operand is the token adjacent to the opcode; a
  // space run before the next word proves there is none and the rest of the
  // segment is display text the legacy route dropped.
  {
    // FA1PLMM0 record 369: `c.cp` + spacing + `The columns ...`; hosted DT
    // 19910927114801 serves `   The columns have the following meaning:`.
    const auto markdown = admit("FA1PLMM0.boo", "6.4.1");
    require(contains(markdown, "The columns have the following meaning:"),
            "FA1PLMM0 6.4.1 dropped the c.cp display payload");
    require(!contains(markdown, "c.cp"),
            "FA1PLMM0 6.4.1 printed the c.cp opcode");
  }
  {
    // DREICMST record 600: `c.cp` + `2i`; hosted DT 19911219125856 serves no
    // `2i` in 2.20.3.1.4, so the unit-suffixed count is an operand.
    const auto markdown = admit("DREICMST.boo", "2.20.3.1.4");
    require(!contains(markdown, "2i") && !contains(markdown, "c.cp"),
            "DREICMST 2.20.3.1.4 printed a c.cp operand");
  }
  {
    // GC28-183 record 91: `c.sp 1 c` after the ST title; hosted DT
    // 19930625102617 serves only the paragraph break.
    const auto markdown = admit("GC28-183.boo", "1.3.3");
    require(!contains(markdown, "c.sp") && !contains(markdown, "1 c"),
            "GC28-183 1.3.3 printed the c.sp control");
    require(contains(markdown, "Requesting Resources"),
            "GC28-183 1.3.3 lost its heading");
  }
}

void negative_fixtures() {
  // Tables and figures compose (tests/prose_topic_spans_synthetic.cpp); a
  // declined envelope still rejects the whole topic.
  reject("ACPZMST1.boo", "FRONT_1.2",
         "table envelope 'TBLUNIQ1' declined: cell text has an unaligned gap: 'AIX/6000                 AIXwindows'");
  reject("ITPPIBOK.BOO", "1.3.7", "picture or external link");
  reject("PRG1SORT.boo", "1.1.5.1", "control-like word 'SRCFILE'");
  // COVER still fails closed: its front-matter `cover` heading form is
  // admitted, but the cover art rows are a placeholder run followed by
  // visible text, which the display-row model does not describe.
  reject("ACPZMST1.boo", "COVER", "is followed by visible text");
  // Two-column definition-list example (`TERMS  DESCRIPTIONS` header): the
  // implied row break misaligns the header spans, so the form fails closed.
  reject("SH20-918.boo", "2.2.2", "exceeds the display line");
  // Plural CFONT header over repeated row controls: the legacy route draws
  // this NetView directory list as a table; prose must not flatten it.
  reject("SC31-711.boo", "1.2", "implicit two-column grid");
  // A box-drawing run that opens no drawn box region and stands inside a
  // text run stays fail-closed (SC24-546 1.3.1, `The >> ___ symbol indicates
  // the beginning of a statement`).
  reject("SC24-546.boo", "1.3.1",
         "is followed by visible text at record 44 token 90");
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


// `CZ` dialect (Format/markup.md, "CZ layout directives").  Every fixture
// below was compared word for word against hosted BookServer: packet
// (DT 20260614112503), SC41-485 (DT 19951003131222), SC09-2417-00 as hosted
// book id `SC09-241` (DT 19961114175628) and GX27-3999-00 as `GX27-399`
// (DT 19950730184057); the local BOO basenames are not the hosted ids for
// the last two (AnalysisNotes/prose-topic-cz-2026-08-28.md).
void cz_fixtures() {
  // `CZ FLOW UL`/`LI` list, `CZ FLOW P` paragraphs across an `SI` term,
  // `CZ OFF XMP` example blocks, `SRFTN`/`CZ FLOW FN` footnotes and their
  // `CSELECT` anchors.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "3.2", &kept);
    require(contains(markdown,
                     "```\n# name callsign speed paclen window description\n"),
            "3.2 example block lost its leading `#`");
    require(contains(markdown,
                     "To define an AX\\.25 port, edit `/etc/ax25/axports`, "
                     "and, use tabs for everything, not spaces:"),
            "3.2 paragraph split at the SI term");
    require(contains(markdown, "[\\(12\\)](<#FTNFTNUNIQ21>)"),
            "3.2 footnote selector");
    require(contains(markdown, "<a id=\"FTNFTNUNIQ21\"></a>"),
            "3.2 footnote anchor id");
    require(contains(markdown, "connections\\)\\!\n"),
            "3.2 footnote kept its row terminator");
    if (kept.prose) {
      require(count_blocks(*kept.prose, ProseBlockKindIR::preformatted) == 4,
              "3.2 example block count");
      require(count_blocks(*kept.prose, ProseBlockKindIR::footnote) == 4,
              "3.2 footnote count");
    }
  }
  // Footnote body ending in a doubled period: hosted prints one.
  {
    const auto markdown = admit("packet.boo", "1.1");
    require(contains(markdown, "medium access control technique\\."),
            "1.1 footnote terminator");
    require(!contains(markdown, "technique\\.\\."), "1.1 doubled terminator");
  }
  // `CZ FLOW DL`/`DT` definition list whose terms are whole `HP2` spans:
  // hosted `<dt>   <B>List</B><dd><B>Configuration</B> ...`.
  {
    Extracted kept;
    const auto markdown = admit("SC41-485.boo", "1.1", &kept);
    require(contains(markdown,
                     "- **List:** **Configuration Descriptions** "
                     "\\(QDCLCFGD\\) returns"),
            "1.1 definition entry");
    require(!contains(markdown, "****List**"),
            "1.1 doubled the term emphasis");
    if (kept.prose)
      require(count_blocks(*kept.prose,
                           ProseBlockKindIR::definition_entry) == 5,
              "1.1 definition entry count");
  }
  // Definition list with unstyled terms and a trailing decoder separator
  // before `cmenu` that hosted does not print.
  {
    const auto markdown = admit("SC41-485.boo", "1.2.5");
    require(contains(markdown, "- **CPF24B4 E:** Severe error while"),
            "1.2.5 definition entry");
  }
  {
    const auto markdown = admit("SC41-485.boo", "1.3.3");
    require(contains(markdown, "[topic 1\\.3\\.4](<#HDRCFGSFLD>)\\."),
            "1.3.3 selector label");
    require(!contains(markdown, "1\\.3\\.4\\.,"),
            "1.3.3 kept the separator before cmenu");
  }
  // `CZ OFF XMP` C++ example: the `{` that a `CFONT` span covers is display
  // text, the uncovered `;` that ends the next row is the row slot.
  {
    const auto markdown = admit("SC09-2417-00.boo", "4.5.2.2");
    require(contains(markdown, "void payroll::calc (employee *pe) {\n"),
            "4.5.2.2 example block lost a brace");
    require(contains(markdown, "> **Note:** In the above program"),
            "4.5.2.2 note block");
  }
  // `CZ FLOW OL` ordered list.
  admit("GX27-3999-00.boo", "2.1");
  admit("SC09-2417-00.boo", "2.1");

  // Mutations of the CZ-only structures.
  auto extracted = extract("packet.boo", "3.2");
  require(extracted.prose.has_value(), "cz mutation fixture rejected");
  if (!extracted.prose) return;
  const auto verify = [&](const ProseTopicIR& topic) {
    return verify_prose_topic_ir(extracted.sources, extracted.layout,
                                 extracted.ownership, extracted.identity.title,
                                 extracted.catalog, topic, nullptr);
  };
  require(verify(*extracted.prose), "cz fixture failed verification");
  {
    auto mutated = *extracted.prose;
    for (auto& block : mutated.blocks)
      if (block.kind == ProseBlockKindIR::preformatted &&
          !block.preformatted_lines.empty()) {
        block.preformatted_lines.front() = "x";
        break;
      }
    require(!verify(mutated), "altered example row passed verification");
  }
  {
    auto mutated = *extracted.prose;
    for (auto& block : mutated.blocks)
      if (block.kind == ProseBlockKindIR::footnote) {
        block.anchor_id = "FTNOTHER";
        break;
      }
    require(!verify(mutated), "altered footnote anchor passed verification");
  }
  {
    auto mutated = *extracted.prose;
    for (auto& block : mutated.blocks)
      if (block.kind == ProseBlockKindIR::list_item) {
        block.ordered = !block.ordered;
        break;
      }
    require(!verify(mutated), "altered list ordering passed verification");
  }
  {
    auto definitions = extract("SC41-485.boo", "1.1");
    require(definitions.prose.has_value(), "SC41-485 1.1 rejected");
    if (definitions.prose) {
      auto mutated = *definitions.prose;
      for (auto& block : mutated.blocks)
        if (block.kind == ProseBlockKindIR::definition_entry) {
          block.term_inline_count += 1;
          break;
        }
      require(!verify_prose_topic_ir(
                  definitions.sources, definitions.layout,
                  definitions.ownership, definitions.identity.title,
                  definitions.catalog, mutated, nullptr),
              "moved definition term boundary passed verification");
    }
  }

  // A closing directive carries the text that follows its region as
  // paragraphs: `cz OFF EXMP 2 2` (packet 2.4.1) and `cz OFF EOL 0 0`
  // (SC09-2417-00 4.2.2), both hosted-verified.
  {
    const auto markdown = admit("packet.boo", "2.4.1");
    require(contains(markdown, "```\n2062:41FE:653A:9882:511:FFE9:8392:412D\n"
                               "```\n\nNote that zeros are omitted"),
            "2.4.1 lost the text on cz OFF EXMP");
  }
  {
    const auto markdown = admit("SC09-2417-00.boo", "4.2.2");
    require(contains(markdown, "The best way to instantiate templates depends"),
            "4.2.2 lost the text on cz OFF EOL");
  }

  // Fail-closed CZ classes: one topic per unmodelled shape.
  // A `cz FLOW DL` header row (hosted `<dl>\n   <B>Option</B>    <B>Tag</B>`)
  // has no typed counterpart yet.
  reject("SC09-2417-00.boo", "3.1.2.2", "cz flow dl carries display text");
  reject("packet.boo", "4.5.1",
         "cz flow h5 without text is not the last directive");
  reject("SC09-2417-00.boo", "1.1.4.3", "cz flow dt");
  // A CFONT span that starts inside a compiled word (hosted
  // `not write<kbd>N4ABC-0</kbd>`, one source token).
  reject("packet.boo", "2.1.1", "span starts inside a word");
}

} // namespace

int main() {
  positive_fixtures();
  mutation_fixtures();
  front_matter_fixtures();
  negative_fixtures();
  cz_fixtures();
  geist_test::exit_with_failures();
  std::cout << "prose_topic_ir_synthetic: ok\n";
  return 0;
}
