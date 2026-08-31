// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Generated CONTENTS/INDEX family: source model, canonical re-extraction
// verifier, mutation negatives, and Document IR lowering.
//
// Every assertion here is anchored on a fixture whose hosted BookServer page
// was compared during the slice that added the family; the hosted evidence is
// named in the message of the assertion it supports.

#include "geist/detail/render/document_markdown_renderer.hpp"
#include "geist/detail/lowering/generated_toc_index_document_lowering.hpp"
#include "geist/detail/ir/generated_toc_index_ir.hpp"
#include "geist/detail/core/internal.hpp"
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
    open_context(std::filesystem::path(GEIST_FIXTURE_DIR) / book,
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
  const auto sources = topic_sources("packet.boo", 7, 11);
  std::string error;
  const auto topic =
      geist::detail::extract_generated_toc_index_topic_ir(sources, nullptr,
                                                          &error);
  require(topic.has_value(), "packet CONTENTS rejected: " + error);
  if (!topic)
    return;
  require(topic->kind == GeneratedTocIndexKindIR::contents &&
              topic->heading_level == ":toc" &&
              topic->title == "Table of Contents",
          "packet CONTENTS envelope is wrong");
  // Record 6 tokens 30..59 hold seven `ctocdef=<n>` lines; the first is
  // `ctocdef=0 1 0 2` (tokens 31..34).
  require(topic->definitions.size() == 7 &&
              topic->definitions.front().ordinal == 0 &&
              topic->definitions.front().operands ==
                  std::vector<std::string>{"1", "0", "2"},
          "packet CTOCDEF definitions are wrong");
  require(topic->entries.size() == 124,
          "packet CONTENTS entry count is " +
              std::to_string(topic->entries.size()));
  const auto &first = topic->entries.front();
  // Record 6 tokens 60..66: length byte 9, `ctoce`, `0`, `1`, `COVER`,
  // `Book`, `Cover`.  Hosted DT 19941010174546 serves
  // `<a name="COVER">COVER</a> ... <strong>Book Cover </strong>`.
  require(first.depth == 0 && first.style == 1 && first.topic_id == "COVER" &&
              first.title == "Book Cover" && !first.topic_id_slices.empty() &&
              !first.title_slices.empty(),
          "packet first CTOCE entry is wrong");
  const auto nested =
      std::find_if(topic->entries.begin(), topic->entries.end(),
                   [](const auto &entry) { return entry.topic_id == "2.1.1"; });
  require(nested != topic->entries.end() && nested->depth == 2,
          "packet nested CTOCE depth is wrong");
  // Hosted keeps the two spaces of `Chapter 1.  Files and Daemons`; the legacy
  // string route collapsed them.
  const auto chapter =
      std::find_if(topic->entries.begin(), topic->entries.end(),
                   [](const auto &entry) { return entry.topic_id == "1.0"; });
  require(chapter != topic->entries.end() &&
              chapter->title == "An Introduction to Packet Radio",
          "packet CTOCE title is wrong");
  require_line_conservation(sources, *topic, "packet CONTENTS");
  require(geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                             *topic, &error),
          "packet CONTENTS canonical verification failed: " + error);

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
  identity.start_logical_record = 7;
  identity.end_logical_record = 11;
  const auto markdown = render(*topic, identity, "packet CONTENTS");
  require(contains(markdown, "- `COVER` [Book Cover](<#COVER>)"),
          "packet CONTENTS lost its top-level entry");
  require(contains(markdown, "  - `1.1` [Original Packet Radio](<#1.1>)"),
          "packet CONTENTS lost its one-level nesting");
  require(contains(markdown, "    - `2.1.1` [Addressing Scheme](<#2.1.1>)"),
          "packet CONTENTS lost its two-level nesting");
  require(contains(markdown, "[Summarize]"),
          "packet CONTENTS lost the BookServer summary link");
}

void verify_index() {
  const auto sources = topic_sources("packet.boo", 369, 372);
  std::string error;
  const auto topic =
      geist::detail::extract_generated_toc_index_topic_ir(sources, nullptr,
                                                          &error);
  require(topic.has_value(), "packet INDEX rejected: " + error);
  if (!topic)
    return;
  require(topic->kind == GeneratedTocIndexKindIR::index &&
              topic->heading_level == ":index" && topic->title == "Index",
          "packet INDEX envelope is wrong");
  // `cidelm` names the one-byte token whose word is U+25BA; every
  // `citerm`/`cgpsep` field is split on that word.
  require(topic->delimiter == 0x25BA,
          "packet CIDELM delimiter is " + std::to_string(topic->delimiter));
  // packet's INDEX carries no `SR<id>` structural anchor; SC31-711's
  // `SRHDRINDEX` pin went with that book (issue #59).
  require(topic->anchors.empty(),
          "packet INDEX gained a structural anchor it does not carry");
  require(!topic->groups.empty() && topic->groups.front().label == "A",
          "packet INDEX first CGPSEP group is wrong");
  const auto *parent = find_term(*topic, "AX.25 Protocol");
  require(parent != nullptr && parent->level == 1 &&
              parent->targets.size() == 1 &&
              parent->targets.front().topic_id == "2.1" &&
              parent->targets.front().range_end_topic_id.empty() &&
              !parent->term_slices.empty(),
          "packet top-level index term is wrong");
  // A child term indented under its parent, carrying its own target.
  const auto *child = find_term(*topic, "Digipeater");
  require(child != nullptr && child->level == 2 &&
              child->targets.size() == 1 &&
              child->targets.front().topic_id == "2.1.3",
          "packet child term level or target is wrong");
  require_line_conservation(sources, *topic, "packet INDEX");
  require(geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                             *topic, &error),
          "packet INDEX canonical verification failed: " + error);

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
  mutated.anchors.emplace_back("OTHER", mutated.groups.front().terms.front()
                                            .term_slices.front());
  require(!geist::detail::verify_generated_toc_index_topic_ir(sources, nullptr,
                                                              mutated, &error),
          "verifier admitted an invented structural anchor");

  geist::detail::TopicIdentityIR identity;
  identity.id = "INDEX";
  identity.title = "Index";
  identity.start_logical_record = 369;
  identity.end_logical_record = 372;
  const auto markdown = render(*topic, identity, "packet INDEX");
  require(!contains(markdown, "<a id="),
          "packet INDEX rendered an anchor it does not carry");
  require(contains(markdown, "## A"),
          "packet INDEX lost its group heading");
  require(contains(markdown, "- AX\\.25 Protocol, [2\\.1](<#2.1>)"),
          "packet INDEX lost a linked term");
  require(contains(markdown, "  - Digipeater, [2\\.1\\.3](<#2.1.3>)"),
          "packet INDEX lost its nesting");
}

// GC23-046's `citerm` range targets (`5.2.4 to 5.3`) and GC28-183's empty
// trailing target field had no counterpart in packet and went with those
// books (issue #59).

void verify_fail_closed() {
  std::string error;
  // SH20-918's term with no text at all was the family's one fail-closed
  // fixture; it went with that book (issue #59).  What remains is the
  // negative that holds for any book: an ordinary prose topic is not a
  // generated navigation topic.
  const auto prose = topic_sources("packet.boo", 31, 33);
  require(!geist::detail::extract_generated_toc_index_topic_ir(prose, nullptr,
                                                               &error),
          "a prose topic was admitted as generated navigation");
  // Neither is a generated list: FIGURES declares `cz OFF FIGLIST`, not a
  // `ctoce`/`citerm` stream.
  const auto figures = topic_sources("packet.boo", 11, 12);
  require(!geist::detail::extract_generated_toc_index_topic_ir(figures, nullptr,
                                                               &error),
          "a generated list was admitted as generated navigation");
}

} // namespace

int main() {
  verify_contents();
  verify_index();
  verify_fail_closed();
  return 0;
}
