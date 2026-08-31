// Ordinary prose topic family (issue #58): typed extraction, conservation,
// lowering and fail-closed classes over real fixtures.
//
// Positive fixtures were compared word for word against hosted BookServer
// (packet DT 20260614112503).  Negative fixtures cover one topic per
// fail-closed class; each must reject with the named reason so a later
// widening of the model is a deliberate change here as well.
//
// This used to run over 27 books.  Only packet.boo may be redistributed, so
// the fixtures that pinned two-column definition rows, screen captures,
// structured `SI` index fields, cross-book `LNK` selectors, `c.<xx>` body
// controls and the empty-`ST` heading forms are gone with them (issue #59).
#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/lowering/prose_topic_document_lowering.hpp"
#include "geist/detail/ir/prose/prose_topic_ir.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
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
        std::filesystem::path(GEIST_FIXTURE_DIR) / file);
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
                                *extracted.ownership, extracted.identity.title,
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

std::size_t count_inlines(const ProseTopicIR& prose, ProseInlineKindIR kind) {
  std::size_t total = 0;
  for (const auto& block : prose.blocks)
    total += static_cast<std::size_t>(
        std::count_if(block.inlines.begin(), block.inlines.end(),
                      [&](const auto& node) { return node.kind == kind; }));
  return total;
}

std::size_t count_blocks(const ProseTopicIR& prose, ProseBlockKindIR kind) {
  return static_cast<std::size_t>(std::count_if(
      prose.blocks.begin(), prose.blocks.end(),
      [&](const auto& block) { return block.kind == kind; }));
}

void positive_fixtures() {
  // Font/selector span geometry (issue #58): a CFONT/CSELECT operand
  // addresses display columns of one display row.
  {
    // A CFONT emphasis span inside a wrapped paragraph, and an `SI` subject
    // index term that is conserved but never displayed.
    Extracted kept;
    const auto markdown = admit("packet.boo", "2.2.1", &kept);
    require(contains(markdown, "NET/ROM often uses *node name aliases\\.*"),
            "2.2.1 lost its emphasis span");
    require(contains(markdown, "your own node could be named `WECNOD`"),
            "2.2.1 lost a monospaced span");
    if (kept.prose) {
      require(!kept.prose->index_terms.empty(),
              "2.2.1 conserved no index term");
      for (const auto& term : kept.prose->index_terms)
        require(!contains(markdown, term.term),
                "2.2.1 rendered the SI index term '" + term.term + "'");
    }
  }
  // A drawn box region: hosted BookServer prints the region's display lines
  // verbatim inside its `<pre>`, so the rows become one preformatted block.
  // The legacy route dropped the outline and reflowed the rows into the body
  // paragraph.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "2.2.1", &kept);
    require(contains(markdown,
                     "```\n    ___ So, what are all these layers? "),
            "2.2.1 box top rule");
    // The row is the whole display line, left rule included: record 50 line
    // 1 is `   | Network stacks are divvied up into "layers," and this
    // notation is used |` and hosted (DT 20260614112503) prints both rules.
    require(contains(markdown,
                     "   | Network stacks are divvied up into \"layers,\" "
                     "and this notation is used |"),
            "2.2.1 box body row");
    require(contains(markdown, "|________"), "2.2.1 box bottom rule");
    if (kept.prose)
      require(count_blocks(*kept.prose, ProseBlockKindIR::preformatted) == 1,
              "2.2.1 preformatted block count");
  }
  // Issue #83: a display row of a verbatim region is one row, and its drawn
  // rules are cells of it.  packet `1.3` carries a `cz OFF LBLBOX` region
  // whose empty rows are stored as record 26 display line 12 --
  // `   ` + `U+2502` + a 63-cell space run + an 8-cell run + `U+2502`, one
  // 77-column line -- and whose text rows are stored as line 13 --
  // `   ` + `U+2502` + the words + `U+2502`.  The reflow cut the empty row at
  // the pair of space runs (`   |` then `        |`) and read the text row's
  // left rule as a revision change bar, spending its column on blanks.
  // Neither decision is the record's: the display-line framing says where the
  // row starts and what it draws.
  {
    const auto markdown = admit("packet.boo", "1.3", nullptr);
    require(contains(markdown,
                     "   |                                            "
                     "                            |"),
            "1.3 empty box row is one whole row");
    require(contains(markdown,
                     "   | For most people using a Baofeng HT or mobile "
                     "radio, the highest  speed |"),
            "1.3 box body row keeps its leading rule");
    require(!contains(markdown, "\n        |\n"),
            "1.3 box row split into fragments");
  }
  // A trailing CMENU validated through the book topic catalog: the labels are
  // the catalog's titles and the destinations are topic ids.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "1.0", &kept);
    require(contains(markdown,
                     "Subtopics:\n\n- [1\\.1 Original Packet Radio](<#1.1>)"),
            "1.0 trailing menu");
    require(contains(markdown, "- [1\\.3 Bringing it Together](<#1.3>)"),
            "1.0 last menu item");
    if (kept.prose)
      require(kept.prose->menu_items.size() == 3, "1.0 menu item count");
  }
  // A menu that follows body prose and a figure in the same topic.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "2.4", &kept);
    require(contains(markdown, "<a id=\"FIGFIGUNIQ16\"></a>"),
            "2.4 figure anchor");
    require(contains(markdown, "*Figure 6\\. IPv4 and IPv6 Packets*"),
            "2.4 figure caption");
    require(contains(markdown, "- [2\\.4\\.1 IPv4 and IPv6](<#2.4.1>)"),
            "2.4 menu after a figure");
    if (kept.prose)
      require(kept.prose->menu_items.size() == 6, "2.4 menu item count");
  }
  // Bullet items with inline code spans and a closing paragraph.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "2.4", &kept);
    require(contains(markdown,
                     "- `TCP` \\(Transmission Control Protocol\\), used to "
                     "set up virtual circuits"),
            "2.4 first list item");
    require(contains(markdown,
                     "- `SCTP` \\(Stream Control Transmission Protocol\\), "
                     "an evolved version of TCP that never caught on"),
            "2.4 last list item");
    if (kept.prose)
      require(count_blocks(*kept.prose, ProseBlockKindIR::list_item) == 4,
              "2.4 list item count");
  }
  // A `Note:` paragraph whose label is an emphasis span.
  {
    const auto markdown = admit("packet.boo", "EDITION");
    require(contains(markdown, "> **Note:** This is the initial version"),
            "EDITION note block");
    require(contains(markdown,
                     "**First Edition, January 27, 2026**"),
            "EDITION lost its styled first line");
  }
  // An emphasis span that begins inside a compiled word: packet 2.1.1 stores
  // one token spelling `writeN4ABC-0` and the span covers only its
  // `N4ABC-0` half, which the reader serves as `not write<kbd>N4ABC-0</kbd>`;
  // the ledger gives each half to its own inline.
  {
    Extracted keep;
    const auto markdown = admit("packet.boo", "2.1.1", &keep);
    require(contains(markdown, "would not write`N4ABC-0`, but would instead"),
            "2.1.1 lost the sub-word highlight");
    bool split = false;
    for (const auto& entry : keep.prose->ledger)
      if (entry.claims.size() > 1) split = true;
    require(split, "2.1.1 has no token split between two inlines");
  }
  // Every typed prose topic of the book extracts, verifies, lowers and
  // verifies again, with no unassigned token anywhere in its ledger.  This is
  // the whole-book conservation sweep the corpus fixtures used to provide.
  {
    auto& loaded = book("packet.boo");
    std::size_t admitted = 0;
    for (const auto& info : loaded.document.topics()) {
      const auto* entry = loaded.document.find_toc_entry(info.id);
      if (entry == nullptr) continue;
      if (entry->render_diagnostic().family != "prose") continue;
      const auto extracted = extract("packet.boo", info.id);
      require(extracted.prose.has_value(),
              "packet " + info.id + " has family prose but was rejected: " +
                  extracted.error);
      if (!extracted.prose) continue;
      std::string error;
      require(verify_prose_topic_ir(
                  extracted.sources, extracted.layout, *extracted.ownership,
                  extracted.identity.title, extracted.catalog,
                  *extracted.prose, &error, extracted.resource_ids),
              "packet " + info.id + " verification failed: " + error);
      for (const auto& entry_row : extracted.prose->ledger)
        require(entry_row.role != ProseTokenRoleIR::unassigned,
                "packet " + info.id + " ledger has an unassigned token");
      ++admitted;
    }
    require(admitted == 119,
            "packet prose family topic count changed: " +
                std::to_string(admitted));
  }
}

void mutation_fixtures() {
  auto extracted = extract("packet.boo", "2.4");
  require(extracted.prose.has_value(), "mutation fixture rejected");
  if (!extracted.prose) return;
  const auto& sources = extracted.sources;
  const auto verify = [&](const ProseTopicIR& mutated) {
    std::string error;
    return verify_prose_topic_ir(sources, extracted.layout, *extracted.ownership,
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
    mutated.menu_items.pop_back();
    require(!verify(mutated), "dropped menu item passed verification");
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
        sources, extracted.layout, *extracted.ownership, "Another Title",
        extracted.catalog, &error, extracted.resource_ids);
    require(!wrong.has_value() && contains(error, "does not match"),
            "title mismatch was admitted, error: " + error);
  }
  // An index term dropped from a topic that carries one.
  {
    auto indexed = extract("packet.boo", "2.2.1");
    require(indexed.prose.has_value(), "index mutation fixture rejected");
    if (indexed.prose && !indexed.prose->index_terms.empty()) {
      auto mutated = *indexed.prose;
      mutated.index_terms.clear();
      require(!verify_prose_topic_ir(
                  indexed.sources, indexed.layout, *indexed.ownership,
                  indexed.identity.title, indexed.catalog, mutated, nullptr),
              "dropped index term passed verification");
    }
  }
}

// Front matter and envelope variants (issue #58).  The corpus used to supply
// nine `CHDLEVEL :<form>` values; packet carries three (issue #59).
void front_matter_fixtures() {
  // `CHDLEVEL :<form>`: front-matter topics name their heading form instead
  // of a level.  Hosted serves every one of them as `<H1>`.
  const struct {
    const char* file;
    const char* id;
    const char* form;
  } forms[] = {
      {"packet.boo", "NOTICES", "notices"},
      {"packet.boo", "EDITION", "vnotice"},
      {"packet.boo", "PREFACE", "preface"},
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

  // A body `SR<id>` anchor keeps its own id, and the display payload that
  // follows it in the same control is text hosted wraps in the anchor.
  {
    Extracted extracted;
    const auto markdown = admit("packet.boo", "A.0", &extracted);
    require(contains(markdown, "<a id=\"HDRURLS\"></a>"),
            "A.0 lost its body anchor");
    require(contains(markdown, "JNOS 2\\.0, Mainline"),
            "A.0 lost the anchor's display payload");
    if (extracted.prose)
      require(!extracted.prose->anchors.empty(), "A.0 anchor provenance");
  }

  // The `ST` title is one display row of its control payload, and the catalog
  // title is a *string* projection of the same control that stops at a
  // different point.  Both are truncations of one word run.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "5.1.2.1", &kept);
    if (kept.prose)
      require(!kept.prose->title.empty(),
              "5.1.2.1 lost its heading across the envelope");
    require(contains(markdown, "####"), "5.1.2.1 heading depth");
  }

  // Deeply nested topic ids keep their own heading level.
  {
    const auto markdown = admit("packet.boo", "5.1.2.1.1");
    require(contains(markdown, "5\\.1\\.2\\.1\\.1 "),
            "5.1.2.1.1 lost its heading number");
  }
}

void negative_fixtures() {
  // One topic per fail-closed class packet still carries; each must reject
  // with the named reason so a later widening of the model is a deliberate
  // change here as well.  The table, screen-capture, implicit-grid and
  // unproven-margin classes went with the books that cannot be published
  // (issue #59).
  reject("packet.boo", "GLOSSARY",
         "placeholder run '?' is followed by visible text");
  // A trailing menu needs the book catalog to validate its targets.
  {
    auto extracted = extract("packet.boo", "1.0");
    std::string error;
    const auto without = extract_prose_topic_ir(
        extracted.sources, extracted.layout, *extracted.ownership,
        extracted.identity.title, nullptr, &error);
    require(!without.has_value() && contains(error, "book topic catalog"),
            "menu admitted without a catalog");
  }
}

// `CZ` dialect (doc/boo-spec/markup.adoc, "CZ layout directives").  Every fixture
// below was compared word for word against hosted BookServer: packet
// (DT 20260614112503).  The SC41-485, SC09-2417-00 and GX27-3999-00 fixtures
// that pinned `CZ FLOW DL`/`DT`, `CZ FLOW OL` and `cz OFF EOL` went with the
// books that cannot be published (issue #59).
void cz_fixtures() {
  // `CZ FLOW UL`/`LI` list, `CZ FLOW P` paragraphs across an `SI` term,
  // `CZ OFF XMP` example blocks, `SRFTN`/`CZ FLOW FN` footnotes and their
  // `CSELECT` anchors.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "3.2", &kept);
    // The `cz OFF XMP` region keeps its own left margin: hosted (packet DT
    // 20260614112503) serves the listing at display column 5,
    // `     # name callsign speed paclen window description`.
    require(contains(
                markdown,
                "```\n     # name callsign speed paclen window description\n"),
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

  // Mutations of the CZ-only structures.
  auto extracted = extract("packet.boo", "3.2");
  require(extracted.prose.has_value(), "cz mutation fixture rejected");
  if (!extracted.prose) return;
  const auto verify = [&](const ProseTopicIR& topic) {
    return verify_prose_topic_ir(extracted.sources, extracted.layout,
                                 *extracted.ownership, extracted.identity.title,
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

  // A closing directive carries the text that follows its region as
  // paragraphs: `cz OFF EXMP 2 2` (packet 2.4.1), hosted-verified.
  {
    const auto markdown = admit("packet.boo", "2.4.1");
    // Hosted (packet DT 20260614112503) serves the address at display
    // column 5, `     2062:41FE:653A:9882:511:FFE9:8392:412D`.
    require(contains(markdown,
                     "```\n     2062:41FE:653A:9882:511:FFE9:8392:412D\n"
                     "```\n\nNote that zeros are omitted"),
            "2.4.1 lost the text on cz OFF EXMP");
  }

  // A trailing `cz FLOW H5` announces the next topic's level, and the
  // footnote that follows it carries a verbatim block of its own: packet
  // 4.5.1 record 225 ends `cz FLOW H5 3 3` / `SRFTNFTNUNIQ50` /
  // `cz FLOW FN 3 7` and then `cz OFF XMP` .. `SREFTN` .. `cz OFF EXMP 6 6`.
  // Hosted (DT 20260614112503) serves six `<pre width="80">` blocks, the
  // last of them under `<a name="FTNFTNUNIQ50">`.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "4.5.1", &kept);
    require(contains(markdown,
                     "```\n         match in on $ext_if proto { 4, 94 } "
                     "rdr-to $lan_ip\n```"),
            "4.5.1 lost the footnote's own example block");
    require(contains(markdown, "<a id=\"FTNFTNUNIQ50\"></a>"),
            "4.5.1 lost its footnote anchor");
    if (kept.prose)
      require(count_blocks(*kept.prose, ProseBlockKindIR::preformatted) == 6,
              "4.5.1 example block count");
  }

  // The generated title-page projection (doc/boo-spec/markup.adoc, "Cover And Title
  // Page Rendering").  `cz OFF TIPAGE` .. `cz OFF ETIPAGE` is not stored
  // prose and not a verbatim region: the compiler laid the source prolog's
  // fields out as display rows and hosted BookServer (DT 20260614112503)
  // re-flows them, flush left, with the `CFONT` triples as per-word emphasis
  // and a paragraph break wherever the source stores a blank row.
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "TITLE", &kept);
    // Hosted serves the three title-block rows as three lines of one
    // paragraph -- the source stores no blank row between them -- and each
    // metadata field as its own paragraph.
    require(contains(markdown,
                     "**Amateur Packet Radio**  \n"
                     "**A Complete Tutorial**  \n"
                     "**Evie Cooper**\n"
                     "\n"
                     "Document Number 9963\\-0413\\-56\n"),
            "TITLE projection");
    // The rows stand at columns 57, 58, 66, 49, 61 and 66; the projection
    // drops every one of them.
    require(!contains(markdown, "          "),
            "TITLE kept the rows' layout origin");
    if (kept.prose) {
      require(count_blocks(*kept.prose, ProseBlockKindIR::paragraph) == 4,
              "TITLE paragraph count");
      require(count_blocks(*kept.prose, ProseBlockKindIR::preformatted) == 0,
              "TITLE was lowered verbatim");
      require(count_inlines(*kept.prose, ProseInlineKindIR::line_break) == 2,
              "TITLE row boundary count");
    }
  }
  {
    Extracted kept;
    const auto markdown = admit("packet.boo", "COVER", &kept);
    // The same projection under `cz OFF COVER`, with a blank row -- and so a
    // paragraph -- before every field.  `Evie Cooper` carries no `CFONT`
    // triple here and hosted serves it plain, while `TITLE` carries
    // `cfont 66 4 2 71 6 2` on the same words and bolds them: the emphasis is
    // read from the operands, never from which field the row holds.
    require(contains(markdown,
                     "**A Complete Tutorial**\n"
                     "\n"
                     "Evie Cooper\n"
                     "\n"
                     "Document Number 9963\\-0413\\-56\n"),
            "COVER projection");
    // The `U+2500` frame rows hosted draws as `<hr>` write no character.
    require(!contains(markdown, "____"), "COVER frame rule drawn as text");
    if (kept.prose) {
      require(count_blocks(*kept.prose, ProseBlockKindIR::paragraph) == 6,
              "COVER paragraph count");
      require(count_inlines(*kept.prose, ProseInlineKindIR::line_break) == 0,
              "COVER row boundary count");
    }
  }
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
