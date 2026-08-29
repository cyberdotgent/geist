#include "geist/boo.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/trap_catalog_document_lowering.hpp"
#include "geist/detail/trap_catalog_ir.hpp"
#include "geist/document.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "trap_catalog_ir_synthetic: " << message << '\n';
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

struct Expectation {
  std::size_t entries = 0;
  std::vector<std::string> vocabulary;
  std::size_t introduction_paragraphs = 0;
  std::string first_id;
  std::string last_id;
};

const geist::detail::TrapEntryIR *
find_entry(const geist::detail::TrapCatalogIR &catalog, const std::string &id) {
  const auto found = std::find_if(
      catalog.entries.begin(), catalog.entries.end(),
      [&](const auto &entry) { return entry.id == id; });
  return found == catalog.entries.end() ? nullptr : &*found;
}

const geist::detail::TrapFieldIR *
find_field(const geist::detail::TrapEntryIR &entry, const std::string &label) {
  const auto found =
      std::find_if(entry.fields.begin(), entry.fields.end(),
                   [&](const auto &field) { return field.label_text == label; });
  return found == entry.fields.end() ? nullptr : &*found;
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  geist::detail::LogicalDecodeContext context;
  open_context(root / "SC31-711.boo", context);
  const auto document = geist::BooDocument::open(root / "SC31-711.boo");

  const std::vector<std::string> response = {"Description:",
                                             "LNM for AIX Response:"};
  const std::map<std::string, Expectation> expected = {
      {"4.1.1", {5, response, 1, "0", "4"}},
      {"4.1.2", {5, {"Description:", "Action:"}, 2, "bridgeHistoryDataComplete",
                 "LNMOS2AgentNotResponding"}},
      {"4.1.3", {78, response, 5, "001", "990"}},
      {"4.2.1", {5, response, 1, "0", "4"}},
      {"4.2.2", {31, response, 1, "1", "805306380"}},
      {"4.3.1", {6, response, 1, "0", "5"}},
      {"4.3.2", {3, response, 1, "256", "258"}},
      {"4.3.3", {2, response, 1, "1", "2"}},
      {"4.3.4", {21, response, 1, "1", "74"}},
      {"4.4", {37, response, 1, "1", "805306374"}},
  };

  for (const auto &[topic_id, expectation] : expected) {
    const auto topic = std::find_if(
        document.topics().begin(), document.topics().end(),
        [&](const auto &candidate) { return candidate.id == topic_id; });
    const auto toc = std::find_if(
        document.table_of_contents().begin(),
        document.table_of_contents().end(),
        [&](const auto &candidate) { return candidate.id == topic_id; });
    require(topic != document.topics().end() &&
                toc != document.table_of_contents().end(),
            topic_id + " is not a SC31-711 topic");
    if (topic == document.topics().end() ||
        toc == document.table_of_contents().end())
      continue;
    const auto sources = geist::detail::decode_logical_record_sources(
        context, topic->start_logical_record, topic->end_logical_record);
    const auto layout = geist::detail::extract_layout_ir(sources);
    const auto ownership = geist::detail::build_ownership_ir(sources, layout);
    std::string error;
    const auto catalog = geist::detail::extract_trap_catalog_ir(
        sources, layout, ownership, toc->title, &error);
    require(catalog.has_value(), topic_id + " rejected: " + error);
    if (!catalog)
      continue;
    require(geist::detail::verify_trap_catalog_ir(sources, layout, ownership,
                                                  *catalog, &error),
            topic_id + " canonical verification failed: " + error);
    require(catalog->raw_topic_id == topic_id && catalog->title == toc->title,
            topic_id + " identity or title differs from the contents");
    require(catalog->entries.size() == expectation.entries,
            topic_id + " entry count " +
                std::to_string(catalog->entries.size()));
    require(catalog->label_vocabulary == expectation.vocabulary,
            topic_id + " label vocabulary differs");
    require(catalog->introduction.size() == expectation.introduction_paragraphs,
            topic_id + " introduction paragraph count " +
                std::to_string(catalog->introduction.size()));
    require(!catalog->entries.empty() &&
                catalog->entries.front().id == expectation.first_id &&
                catalog->entries.back().id == expectation.last_id,
            topic_id + " entry order differs");
    require(catalog->origin_column == 3, topic_id + " origin column");
    for (const auto &entry : catalog->entries) {
      std::vector<std::string> labels;
      for (const auto &field : entry.fields)
        if (field.in_vocabulary)
          labels.push_back(field.label_text);
      require(labels == catalog->label_vocabulary,
              topic_id + " entry " + entry.id + " lacks the vocabulary");
      require(entry.headline.body.text.compare(0, entry.id.size(),
                                               entry.id) == 0,
              topic_id + " entry " + entry.id + " headline");
      require(!entry.cells.empty(), topic_id + " entry " + entry.id +
                                        " has no ledgered cells");
    }

    // Lowering and its canonical verification.
    geist::detail::TopicIdentityIR identity;
    identity.id = topic_id;
    identity.title = toc->title;
    identity.heading_level = topic->heading_level;
    identity.topic_number = topic->topic_number;
    identity.start_logical_record = topic->start_logical_record;
    identity.end_logical_record = topic->end_logical_record;
    auto lowered = geist::detail::lower_trap_catalog_to_document_ir(
        identity, *catalog, &error);
    require(lowered.has_value(), topic_id + " lowering failed: " + error);
    if (lowered) {
      require(geist::detail::verify_trap_catalog_document_ir(*catalog, *lowered,
                                                             &error),
              topic_id + " document verification failed: " + error);
      const auto anchors = static_cast<std::size_t>(std::count_if(
          lowered->blocks.begin(), lowered->blocks.end(), [](const auto &block) {
            return std::holds_alternative<geist::detail::AnchorBlockIR>(
                block.node);
          }));
      require(anchors == catalog->entries.size() + catalog->anchors.size(),
              topic_id + " anchor blocks");
      auto mutated = *lowered;
      for (auto &block : mutated.blocks)
        if (auto *list =
                std::get_if<geist::detail::ListBlockIR>(&block.node))
          for (auto &item : list->items)
            for (auto &inline_node : item.content)
              if (auto *text = std::get_if<geist::detail::TextInlineIR>(
                      &inline_node.node))
                text->text += " x";
      require(!geist::detail::verify_trap_catalog_document_ir(*catalog, mutated,
                                                              nullptr),
              topic_id + " document mutation was not rejected");
    }
  }

  // Focused source evidence on 4.4 (FDDI): a record-prefix label, a deferred
  // headline, a wrapped row gap, an entry-local Note: line, and the words the
  // legacy flattening dropped before wide padding runs.
  {
    const auto topic = std::find_if(
        document.topics().begin(), document.topics().end(),
        [](const auto &candidate) { return candidate.id == "4.4"; });
    const auto sources = geist::detail::decode_logical_record_sources(
        context, topic->start_logical_record, topic->end_logical_record);
    const auto layout = geist::detail::extract_layout_ir(sources);
    const auto ownership = geist::detail::build_ownership_ir(sources, layout);
    std::string error;
    auto catalog = geist::detail::extract_trap_catalog_ir(
        sources, layout, ownership, "FDDI SNMP Proxy Agent Traps", &error);
    require(catalog.has_value(), "4.4 rejected: " + error);
    if (catalog) {
      require(catalog->anchors.size() == 2 && catalog->anchors[0].id == "MSG" &&
                  catalog->anchors[1].id == "HDRFDDITRP",
              "4.4 named anchors");
      require(catalog->introduction.size() == 1 &&
                  catalog->introduction.front().text.find(
                      "each of these traps, refer to IBM FDDI SNMP Proxy Agent "
                      "User's Guide.") != std::string::npos,
              "4.4 introduction joins the CFONT continuation without the "
              "terminal `/` glyph");
      const auto *entry_4 = find_entry(*catalog, "4");
      const auto *entry_7 = find_entry(*catalog, "7");
      const auto *entry_8 = find_entry(*catalog, "8");
      const auto *entry_13 = find_entry(*catalog, "13");
      const auto *hold = find_entry(*catalog, "805306372");
      require(entry_4 != nullptr && entry_7 != nullptr && entry_8 != nullptr &&
                  entry_13 != nullptr && hold != nullptr,
              "4.4 entries 4/7/8/13/805306372 are present");
      if (entry_4 != nullptr) {
        const auto *field = find_field(*entry_4, "LNM for AIX Response:");
        require(field != nullptr &&
                    field->line.body.text.rfind(
                        "LNM for AIX Response: rpuBadLoadReceived set", 0) == 0,
                "4.4 entry 4 record-prefix label row is the field line");
        const auto *description = find_field(*entry_4, "Description:");
        require(description != nullptr &&
                    description->line.body.text.find(
                        "the load file received was incorrect or corrupted.") !=
                        std::string::npos,
                "4.4 entry 4 keeps the lexical `was` marker");
      }
      if (entry_7 != nullptr)
        require(entry_7->headline.body.text ==
                        "7 (fddiControlCartridgeError)" &&
                    entry_7->headline.spans.size() == 2,
                "4.4 entry 7 deferred headline: " + entry_7->headline.body.text);
      if (entry_8 != nullptr) {
        const auto *description = find_field(*entry_8, "Description:");
        require(description != nullptr &&
                    description->line.body.text.find(
                        "a ring interface cartridge error.") !=
                        std::string::npos,
                "4.4 entry 8 keeps `cartridge` before the wide padding run");
      }
      if (entry_13 != nullptr) {
        const auto *description = find_field(*entry_13, "Description:");
        require(description != nullptr &&
                    description->line.body.text.find(
                        "received indicating that the Availability") !=
                        std::string::npos,
                "4.4 entry 13 keeps `indicating` before the wide padding run");
      }
      if (hold != nullptr) {
        require(hold->fields.size() == 3 && hold->fields[1].label_text == "Note:" &&
                    !hold->fields[1].in_vocabulary &&
                    hold->fields[0].in_vocabulary && hold->fields[2].in_vocabulary,
                "4.4 entry 805306372 keeps its entry-local Note: outside the "
                "vocabulary");
      }
      const auto *state_change = find_entry(*catalog, "1879048231");
      if (state_change != nullptr) {
        const auto *description = find_field(*state_change, "Description:");
        require(description != nullptr &&
                    description->line.body.text.find("frame is received "
                                                     "indicating that the CF "
                                                     "state") !=
                        std::string::npos,
                "4.4 entry 1879048231 treats the origin-3 `:` slot as layout");
      }
      const auto *duplicate = find_entry(*catalog, "1879048236");
      if (duplicate != nullptr) {
        const auto *field = find_field(*duplicate, "LNM for AIX Response:");
        require(field != nullptr &&
                    field->line.body.text.find(
                        "cleared in segment. Segment is resynchronized.") !=
                        std::string::npos,
                "4.4 entry 1879048236 keeps the sentence stop before a "
                "placeholder");
      }

      // Every ledgered cell is claimed exactly once and matches its source.
      for (const auto &entry : catalog->entries) {
        std::vector<std::tuple<std::uint32_t, std::size_t, std::size_t>> keys;
        for (const auto &cell : entry.cells)
          keys.emplace_back(cell.logical_record, cell.token_index,
                            cell.word_index);
        auto sorted = keys;
        std::sort(sorted.begin(), sorted.end());
        require(std::adjacent_find(sorted.begin(), sorted.end()) ==
                    sorted.end(),
                "4.4 entry " + entry.id + " ledgers a cell twice");
      }

      // Mutations are rejected by the canonical verifier.
      auto text_mutation = *catalog;
      text_mutation.entries.front().fields.front().line.body.text += " x";
      require(!geist::detail::verify_trap_catalog_ir(
                  sources, layout, ownership, text_mutation, nullptr),
              "4.4 field text mutation was not rejected");
      auto label_mutation = *catalog;
      label_mutation.label_vocabulary.front() = "Meaning:";
      require(!geist::detail::verify_trap_catalog_ir(
                  sources, layout, ownership, label_mutation, nullptr),
              "4.4 vocabulary mutation was not rejected");
      auto cell_mutation = *catalog;
      cell_mutation.entries.front().cells.pop_back();
      require(!geist::detail::verify_trap_catalog_ir(
                  sources, layout, ownership, cell_mutation, nullptr),
              "4.4 dropped ledger cell was not rejected");
      auto order_mutation = *catalog;
      std::swap(order_mutation.entries[0], order_mutation.entries[1]);
      require(!geist::detail::verify_trap_catalog_ir(
                  sources, layout, ownership, order_mutation, nullptr),
              "4.4 entry order mutation was not rejected");
      auto intro_mutation = *catalog;
      intro_mutation.introduction.clear();
      require(!geist::detail::verify_trap_catalog_ir(
                  sources, layout, ownership, intro_mutation, nullptr),
              "4.4 introduction mutation was not rejected");
    }
  }

  // 4.3.5 used to fail closed: the `cbacklevel` its introduction appeared to
  // carry was a display line's length byte, not a control.  With the byte
  // read as a length the catalog composes, and hosted DT 19941010174546
  // serves exactly its one entry (`1` / `Description:  DLCI state change` /
  // `LNM for AIX Response:  Poll the port.`).
  {
    const auto topic = std::find_if(
        document.topics().begin(), document.topics().end(),
        [](const auto &candidate) { return candidate.id == "4.3.5"; });
    const auto sources = geist::detail::decode_logical_record_sources(
        context, topic->start_logical_record, topic->end_logical_record);
    const auto layout = geist::detail::extract_layout_ir(sources);
    const auto ownership = geist::detail::build_ownership_ir(sources, layout);
    std::string error;
    const auto catalog = geist::detail::extract_trap_catalog_ir(
        sources, layout, ownership, "Frame Relay Redirected Traps", &error);
    require(catalog.has_value() && catalog->entries.size() == 1 &&
                catalog->entries.front().id == "1",
            "4.3.5 trap catalog was not composed: " + error);
  }

  return 0;
}
