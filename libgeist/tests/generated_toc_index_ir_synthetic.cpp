// Generated CONTENTS/INDEX family: source model, canonical re-extraction
// verifier, mutation negatives, and Document IR lowering.
//
// Every assertion here is anchored on a fixture whose hosted BookServer page
// was compared during the slice that added the family; the hosted evidence is
// named in the message of the assertion it supports.

#include "geist/detail/document_markdown_renderer.hpp"
#include "geist/detail/generated_toc_index_document_lowering.hpp"
#include "geist/detail/generated_toc_index_ir.hpp"
#include "geist/detail/internal.hpp"
#include "test_failures.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using geist::detail::GeneratedTocIndexKindIR;
using geist::detail::GeneratedTocIndexTopicIR;

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "generated_toc_index_ir_synthetic: " << message << '\n';
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

std::vector<geist::detail::DecodedLogicalRecordSource> topic_sources(
    const std::string &book, std::uint32_t first, std::uint32_t end) {
  static std::vector<
      std::pair<std::string,
                std::shared_ptr<geist::detail::LogicalDecodeContext>>>
      cache;
  const auto found =
      std::find_if(cache.begin(), cache.end(),
                   [&](const auto &entry) { return entry.first == book; });
  if (found == cache.end()) {
    auto context = std::make_shared<geist::detail::LogicalDecodeContext>();
    open_context(std::filesystem::path(GEIST_REPO_ROOT) / "BOO" / book,
                 *context);
    cache.emplace_back(book, context);
    return geist::detail::decode_logical_record_sources(*context, first, end);
  }
  return geist::detail::decode_logical_record_sources(*found->second, first,
                                                      end);
}

// The line ledger is the family's conservation proof: every display line of
// every record is typed exactly once, and the line token ranges partition each
// record's token stream gaplessly and in order.
void require_line_conservation(
    const std::vector<geist::detail::DecodedLogicalRecordSource> &sources,
    const GeneratedTocIndexTopicIR &topic, const std::string &what) {
  std::size_t line = 0;
  for (const auto &record : sources) {
    std::size_t next_token = 0;
    while (line < topic.lines.size() &&
           topic.lines[line].source.logical_record == record.logical_record) {
      const auto &slice = topic.lines[line].source;
      require(slice.token_begin == next_token,
              what + ": line ledger leaves a gap in record " +
                  std::to_string(record.logical_record));
      require(slice.token_end > slice.token_begin,
              what + ": line ledger has an empty range");
      require(slice.byte_begin ==
                      record.ir.tokens[slice.token_begin].byte_range.begin &&
                  slice.byte_end ==
                      record.ir.tokens[slice.token_end - 1].byte_range.end,
              what + ": line ledger byte range disagrees with its tokens");
      next_token = slice.token_end;
      ++line;
    }
    require(next_token == record.ir.tokens.size(),
            what + ": line ledger does not reach the end of record " +
                std::to_string(record.logical_record));
  }
  require(line == topic.lines.size(),
          what + ": line ledger names a record outside the topic");
}

const geist::detail::GeneratedIndexTermIR *find_term(
    const GeneratedTocIndexTopicIR &topic, const std::string &term) {
  for (const auto &group : topic.groups)
    for (const auto &entry : group.terms)
      if (entry.term == term)
        return &entry;
  return nullptr;
}

std::string render(const GeneratedTocIndexTopicIR &topic,
                   geist::detail::TopicIdentityIR identity,
                   const std::string &what) {
  std::string error;
  const auto document =
      geist::detail::lower_generated_toc_index_topic_to_document_ir(
          identity, topic, &error);
  require(document.has_value(), what + ": lowering rejected: " + error);
  if (!document)
    return {};
  require(geist::detail::verify_generated_toc_index_topic_document_ir(
              topic, *document, &error),
          what + ": canonical document verification failed: " + error);
  return geist::detail::render_document_markdown(*document);
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

void verify_contents() {
  const auto sources = topic_sources("SC31-711.boo", 6, 9);
  std::string error;
  const auto topic =
      geist::detail::extract_generated_toc_index_topic_ir(sources, nullptr,
                                                          &error);
  require(topic.has_value(), "SC31-711 CONTENTS rejected: " + error);
  if (!topic)
    return;
  require(topic->kind == GeneratedTocIndexKindIR::contents &&
              topic->heading_level == ":toc" &&
              topic->title == "Table of Contents",
          "SC31-711 CONTENTS envelope is wrong");
  // Record 6 tokens 30..59 hold seven `ctocdef=<n>` lines; the first is
  // `ctocdef=0 1 0 2` (tokens 31..34).
  require(topic->definitions.size() == 7 &&
              topic->definitions.front().ordinal == 0 &&
              topic->definitions.front().operands ==
                  std::vector<std::string>{"1", "0", "2"},
          "SC31-711 CTOCDEF definitions are wrong");
  require(topic->entries.size() == 82,
          "SC31-711 CONTENTS entry count is " +
              std::to_string(topic->entries.size()));
  const auto &first = topic->entries.front();
  // Record 6 tokens 60..66: length byte 9, `ctoce`, `0`, `1`, `COVER`,
  // `Book`, `Cover`.  Hosted DT 19941010174546 serves
  // `<a name="COVER">COVER</a> ... <strong>Book Cover </strong>`.
  require(first.depth == 0 && first.style == 1 && first.topic_id == "COVER" &&
              first.title == "Book Cover" && !first.topic_id_slices.empty() &&
              !first.title_slices.empty(),
          "SC31-711 first CTOCE entry is wrong");
  const auto nested =
      std::find_if(topic->entries.begin(), topic->entries.end(),
                   [](const auto &entry) { return entry.topic_id == "2.1.1"; });
  require(nested != topic->entries.end() && nested->depth == 2 &&
              nested->style == 3,
          "SC31-711 nested CTOCE depth is wrong");
  // Hosted keeps the two spaces of `Chapter 1.  Files and Daemons`; the legacy
  // string route collapsed them.
  const auto chapter =
      std::find_if(topic->entries.begin(), topic->entries.end(),
                   [](const auto &entry) { return entry.topic_id == "1.0"; });
  require(chapter != topic->entries.end() &&
              chapter->title == "Chapter 1.  Files and Daemons",
          "SC31-711 CTOCE title lost its source spacing");
  require_line_conservation(sources, *topic, "SC31-711 CONTENTS");
  require(geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                             *topic, &error),
          "SC31-711 CONTENTS canonical verification failed: " + error);

  auto mutated = *topic;
  mutated.entries.front().title = "Book Covers";
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a mutated CTOCE title");
  mutated = *topic;
  mutated.entries.front().depth = 1;
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a mutated CTOCE depth");
  mutated = *topic;
  ++mutated.entries.front().topic_id_slices.front().token_end;
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted mutated CTOCE topic-id provenance");
  mutated = *topic;
  ++mutated.heading_source.byte_end;
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted mutated heading provenance");
  mutated = *topic;
  mutated.lines.erase(mutated.lines.begin() + 9);
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a line ledger with a hole");
  mutated = *topic;
  mutated.definitions.front().operands.front() = "9";
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a mutated CTOCDEF operand");

  geist::detail::TopicIdentityIR identity;
  identity.id = "CONTENTS";
  identity.title = "Table of Contents";
  identity.start_logical_record = 6;
  identity.end_logical_record = 9;
  const auto markdown = render(*topic, identity, "SC31-711 CONTENTS");
  require(contains(markdown, "- `COVER` [Book Cover](<#COVER>)"),
          "SC31-711 CONTENTS lost its top-level entry");
  require(contains(markdown, "  - `FRONT_1.1` [Trademarks](<#FRONT_1.1>)"),
          "SC31-711 CONTENTS lost its one-level nesting");
  require(contains(markdown,
                   "    - `2.1.1` [Displaying LNM for AIX Status "
                   "Information](<#2.1.1>)"),
          "SC31-711 CONTENTS lost its two-level nesting");
  require(contains(markdown, "[Summarize]"),
          "SC31-711 CONTENTS lost the BookServer summary link");
}

void verify_index() {
  const auto sources = topic_sources("SC31-711.boo", 538, 541);
  std::string error;
  const auto topic =
      geist::detail::extract_generated_toc_index_topic_ir(sources, nullptr,
                                                          &error);
  require(topic.has_value(), "SC31-711 INDEX rejected: " + error);
  if (!topic)
    return;
  require(topic->kind == GeneratedTocIndexKindIR::index &&
              topic->heading_level == ":index" && topic->title == "Index",
          "SC31-711 INDEX envelope is wrong");
  // Record 538 token 31 is `cidelm`, token 32 is the one-byte token whose word
  // is U+25BA; every `citerm`/`cgpsep` field is split on that word.
  require(topic->delimiter == 0x25BA,
          "SC31-711 CIDELM delimiter is " + std::to_string(topic->delimiter));
  // Record 538 token 25 is `SRHDRINDEX`; hosted serves the anchor name without
  // the `SR` prefix.
  require(topic->anchors.size() == 1 &&
              topic->anchors.front().first == "HDRINDEX",
          "SC31-711 INDEX structural anchor is wrong");
  require(!topic->groups.empty() && topic->groups.front().label == "A" &&
              topic->groups.front().terms.size() == 3,
          "SC31-711 INDEX first CGPSEP group is wrong");
  const auto *adapter = find_term(*topic, "adapter problems");
  require(adapter != nullptr && adapter->level == 1 &&
              adapter->targets.size() == 1 &&
              adapter->targets.front().topic_id == "2.2.4" &&
              adapter->targets.front().range_end_topic_id.empty() &&
              !adapter->term_slices.empty(),
          "SC31-711 `adapter problems` term is wrong");
  // `citerm <D>description<D>1` carries no target field at all; hosted serves
  // it as plain text above its indented children.
  const auto *description = find_term(*topic, "description");
  require(description != nullptr && description->level == 1 &&
              description->targets.empty(),
          "SC31-711 targetless parent term is wrong");
  const auto *directories = find_term(*topic, "directories");
  require(directories != nullptr && directories->level == 2,
          "SC31-711 child term level is wrong");
  require_line_conservation(sources, *topic, "SC31-711 INDEX");
  require(geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                             *topic, &error),
          "SC31-711 INDEX canonical verification failed: " + error);

  auto mutated = *topic;
  mutated.delimiter = 0x2666;
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a mutated CIDELM delimiter");
  mutated = *topic;
  mutated.groups.front().label = "B";
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a mutated CGPSEP label");
  mutated = *topic;
  mutated.groups.front().terms.front().level = 2;
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a mutated CITERM level");
  mutated = *topic;
  mutated.groups.front().terms.front().targets.front().topic_id = "9.9";
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a mutated CITERM target");
  mutated = *topic;
  ++mutated.groups.front().terms.front().term_slices.front().token_begin;
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted mutated CITERM term provenance");
  mutated = *topic;
  mutated.anchors.front().first = "OTHER";
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted a mutated structural anchor");

  geist::detail::TopicIdentityIR identity;
  identity.id = "INDEX";
  identity.title = "Index";
  identity.start_logical_record = 538;
  identity.end_logical_record = 541;
  const auto markdown = render(*topic, identity, "SC31-711 INDEX");
  require(contains(markdown, "<a id=\"HDRINDEX\"></a>"),
          "SC31-711 INDEX lost its source anchor");
  require(contains(markdown, "## A"),
          "SC31-711 INDEX lost its group heading");
  require(contains(markdown, "- adapter problems, [2\\.2\\.4](<#2.2.4>)"),
          "SC31-711 INDEX lost a linked term");
  require(contains(markdown, "- description\n  - directories, "),
          "SC31-711 INDEX lost the targetless parent or its nesting");
}

void verify_range_and_empty_targets() {
  // GC23-046 record 293 `citerm <D>target system<D>2<D>5.2.4 to 5.3`.  Hosted
  // DT 19920330095121 serves it as two links joined by the word `to`.
  const auto gc23 = topic_sources("GC23-046.boo", 285, 295);
  std::string error;
  const auto index =
      geist::detail::extract_generated_toc_index_topic_ir(gc23, nullptr,
                                                          &error);
  require(index.has_value(), "GC23-046 INDEX rejected: " + error);
  if (index) {
    const geist::detail::GeneratedIndexTermIR *ranged = nullptr;
    for (const auto &group : index->groups)
      for (const auto &term : group.terms)
        for (const auto &target : term.targets)
          if (!target.range_end_topic_id.empty())
            ranged = &term;
    require(ranged != nullptr && ranged->term == "target system" &&
                ranged->level == 2 && ranged->targets.size() == 1 &&
                ranged->targets.front().topic_id == "5.2.4" &&
                ranged->targets.front().range_end_topic_id == "5.3",
            "GC23-046 range target is wrong");
    require_line_conservation(gc23, *index, "GC23-046 INDEX");
    auto mutated = *index;
    for (auto &group : mutated.groups)
      for (auto &term : group.terms)
        for (auto &target : term.targets)
          target.range_end_topic_id.clear();
    require(!geist::detail::verify_generated_toc_index_topic_ir(gc23, nullptr,
                                                                mutated,
                                                                &error),
            "verifier admitted a range target with its end dropped");
    geist::detail::TopicIdentityIR identity;
    identity.id = "INDEX";
    identity.title = "Index";
    identity.start_logical_record = 285;
    identity.end_logical_record = 295;
    const auto markdown = render(*index, identity, "GC23-046 INDEX");
    require(contains(markdown,
                     "target system, [5\\.2\\.4](<#5.2.4>) to "
                     "[5\\.3](<#5.3>)"),
            "GC23-046 range target did not render as two links");
  }

  // GC28-183 record 917 `citerm <D>//*DATASET statement<D>1<D>` ends on an
  // empty target field: the term is a parent whose children carry the targets.
  const auto gc28 = topic_sources("GC28-183.boo", 917, 976);
  const auto other =
      geist::detail::extract_generated_toc_index_topic_ir(gc28, nullptr,
                                                          &error);
  require(other.has_value(), "GC28-183 INDEX rejected: " + error);
  if (other) {
    const auto *dataset = find_term(*other, "//*DATASET statement");
    require(dataset != nullptr && dataset->level == 1 &&
                dataset->targets.empty(),
            "GC28-183 empty trailing target field is wrong");
    require(!other->groups.empty() &&
                other->groups.front().label == "Special Characters",
            "GC28-183 first group label is wrong");
    require_line_conservation(gc28, *other, "GC28-183 INDEX");
  }
}

void verify_fail_closed() {
  std::string error;
  // SH20-918 record 636 token 275 is a two-byte token spelling the delimiter
  // twice, so that `citerm` states a level and a `See` child but no term text
  // at all.  Nothing may be invented for it, and hosted DT 19910520154851
  // truncates its own output before that group, so the whole topic fails
  // closed.
  const auto sh20 = topic_sources("SH20-918.boo", 608, 637);
  require(!geist::detail::extract_generated_toc_index_topic_ir(sh20, nullptr,
                                                               &error),
          "SH20-918 INDEX was admitted despite a term with no text");
  require(error.find("no term text") != std::string::npos,
          "SH20-918 INDEX was rejected for the wrong reason: " + error);

  // An ordinary prose topic is not a generated navigation topic.
  const auto prose = topic_sources("SC31-711.boo", 19, 21);
  require(!geist::detail::extract_generated_toc_index_topic_ir(prose, nullptr,
                                                               &error),
          "a prose topic was admitted as generated navigation");
}

} // namespace

int main() {
  verify_contents();
  verify_index();
  verify_range_and_empty_targets();
  verify_fail_closed();
  return 0;
}
