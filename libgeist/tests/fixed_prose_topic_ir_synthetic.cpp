#include "geist/detail/fixed_prose_document_lowering.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace geist;
using namespace geist::detail;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "fixed_prose_topic_ir_synthetic: " << message << '\n';
    std::exit(1);
  }
}

struct LoadedBook {
  BooDocument document;
  LogicalDecodeContext context;

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
  }
};

const TopicInfo& topic(const LoadedBook& book, const std::string& id) {
  const auto found = std::find_if(
      book.document.topics().begin(), book.document.topics().end(),
      [&](const auto& candidate) { return candidate.id == id; });
  require(found != book.document.topics().end(), "missing fixture topic " + id);
  return *found;
}

struct Extracted {
  std::vector<DecodedLogicalRecordSource> sources;
  LayoutIR layout;
  OwnershipIR ownership;
  std::optional<FixedProseTopicIR> prose;
  std::string error;
};

Extracted extract(LoadedBook& book, const std::string& id) {
  const auto& info = topic(book, id);
  Extracted result;
  result.sources = decode_logical_record_sources(
      book.context, info.start_logical_record, info.end_logical_record);
  result.layout = extract_layout_ir(result.sources);
  result.ownership = build_ownership_ir(result.sources, result.layout);
  result.prose = extract_fixed_prose_topic_ir(
      result.sources, result.layout, result.ownership, &result.error);
  return result;
}

TopicIdentityIR identity(const TopicInfo& topic) {
  return {topic.id, topic.title, topic.heading_level, topic.topic_number,
          topic.start_logical_record, topic.end_logical_record};
}

std::string text(const BlockIR& block) {
  if (const auto* heading = std::get_if<HeadingBlockIR>(&block.node))
    return std::get<TextInlineIR>(heading->content.front().node).text;
  const auto* paragraph = std::get_if<ParagraphBlockIR>(&block.node);
  return paragraph == nullptr
             ? std::string{}
             : std::get<TextInlineIR>(paragraph->content.front().node).text;
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  LoadedBook itp(root / "ITPPIBOK.BOO");
  LoadedBook gg24(root / "GG24-4302-00.boo");
  LoadedBook sc31(root / "SC31-711.boo");

  auto communications = extract(itp, "2.1.2");
  require(communications.prose.has_value(), communications.error);
  require(communications.prose->heading_level == "h3" &&
              !communications.prose->anchor &&
              communications.prose->segments.size() == 9 &&
              communications.prose->prose.title ==
                  "Communications Controller Requirements" &&
              communications.prose->prose.paragraph.find(
                  "TPNS CHEAPP (Channel end)") != std::string::npos,
          "unanchored fixed prose envelope changed");
  require(communications.prose->payload_bytes.begin ==
                  communications.sources.front().ir.payload_range.begin &&
              communications.prose->payload_bytes.end ==
                  communications.sources.front().ir.payload_range.end &&
              communications.prose->token_count ==
                  communications.sources.front().ir.tokens.size() &&
              communications.prose->heading_source.byte_begin <
                  communications.prose->heading_source.byte_end &&
              communications.prose->paragraph_source.byte_begin <
                  communications.prose->paragraph_source.byte_end,
          "whole-record or semantic source provenance is incomplete");
  require(verify_fixed_prose_topic_ir(
              communications.sources, communications.layout,
              communications.ownership, *communications.prose,
              &communications.error),
          communications.error);

  auto expected_results = extract(itp, "4.1.2");
  require(expected_results.prose.has_value(), expected_results.error);
  require(expected_results.prose->heading_level == "h3" &&
              expected_results.prose->anchor &&
              expected_results.prose->anchor->id == "HDRPLNEXR" &&
              expected_results.prose->segments.size() == 10 &&
              expected_results.prose->prose.title == "Expected Results",
          "anchored fixed prose envelope changed");

  std::string error;
  const auto document = lower_fixed_prose_topic_to_document_ir(
      identity(topic(itp, "4.1.2")), *expected_results.prose, &error);
  require(document.has_value(), error);
  require(document->blocks.size() == 3 &&
              text(document->blocks[0]) == "Expected Results" &&
              std::get<AnchorBlockIR>(document->blocks[1].node).id ==
                  "HDRPLNEXR" &&
              text(document->blocks[2]).find("60 logon requests") !=
                  std::string::npos &&
              verify_fixed_prose_topic_document_ir(
                  *expected_results.prose, *document, &error),
          error.empty() ? "fixed prose DocumentIR changed" : error);

  auto changed_source = *expected_results.prose;
  changed_source.segments.back().opcode = "ST-mutated";
  require(!verify_fixed_prose_topic_ir(
              expected_results.sources, expected_results.layout,
              expected_results.ownership, changed_source, &error),
          "canonical verifier admitted changed envelope provenance");
  auto changed_document = *document;
  std::get<TextInlineIR>(
      std::get<ParagraphBlockIR>(changed_document.blocks.back().node)
          .content.front().node)
      .text += " mutation";
  require(!verify_fixed_prose_topic_document_ir(
              *expected_results.prose, changed_document, &error),
          "canonical verifier admitted changed prose output");

  // These nine topics contain an ST shape but not a complete fixed-prose
  // topic. Their selectors, CFONT, menu, message, or trailing text must never
  // be discarded by admitting only the inner ST segment.
  const std::vector<std::tuple<LoadedBook*, std::string, std::string>>
      rejected = {
      {&gg24, "3.2.11.1", "trailing text"},
      {&gg24, "5.4.2", "trailing CFONT"},
      {&itp, "PREFACE.2", "selectors"},
      {&itp, "2.4.2.2", "trailing CFONT"},
      {&itp, "3.0", "menu"},
      {&sc31, "PREFACE", "menu"},
      {&sc31, "3.0", "text and menu"},
      {&sc31, "4.2.1", "message and CFONT"},
      {&sc31, "4.3.3", "message and CFONT"},
  };
  for (const auto& [book, id, reason] : rejected) {
    const auto candidate = extract(*book, id);
    require(!candidate.prose,
            id + " admitted despite out-of-envelope " + reason);
  }

  std::cout << "fixed prose whole-topic inventory: 2 accepted, 9 rejected\n";
  return 0;
}
