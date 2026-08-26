#include "geist/detail/generated_list_document_lowering.hpp"
#include "geist/detail/generated_list_topic_ir.hpp"
#include "geist/detail/internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using geist::detail::DecodedLogicalRecordSource;
using geist::detail::TokenWords;

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

TokenWords words(const std::string& text) {
  return TokenWords(text.begin(), text.end());
}

DecodedLogicalRecordSource make_source(std::uint32_t logical_record,
                                       std::vector<TokenWords> tokens) {
  DecodedLogicalRecordSource source;
  source.logical_record = logical_record;
  source.tokens = std::move(tokens);
  source.encoded_tokens.resize(source.tokens.size());
  source.ir.logical_record = logical_record;
  auto byte = static_cast<std::uint32_t>(logical_record * 100);
  for (std::size_t index = 0; index < source.tokens.size(); ++index) {
    source.encoded_tokens[index] = {static_cast<std::uint16_t>(0x20 + index), 1};
    source.ir.tokens.push_back(
        {index, source.encoded_tokens[index], source.tokens[index],
         {byte, static_cast<std::uint32_t>(byte + 1)}, false, 3});
    ++byte;
  }
  source.ir.payload_range =
      {static_cast<std::uint32_t>(logical_record * 100), byte};
  source.assembled =
      geist::detail::assemble_logical_record_with_sources(source.tokens);
  source.control_segments = geist::detail::decode_control_segments(
      source.logical_record, source.assembled);
  return source;
}

struct Pipeline {
  std::vector<DecodedLogicalRecordSource> sources;
  geist::detail::SelectorCatalogIR selectors;
  geist::detail::LayoutIR layout;
  geist::detail::OwnershipIR ownership;
};

Pipeline pipeline(std::vector<DecodedLogicalRecordSource> sources) {
  Pipeline result;
  result.sources = std::move(sources);
  std::string error;
  const auto selectors =
      geist::detail::extract_selector_catalog_ir(result.sources, &error);
  require(selectors.has_value(), "selector extraction failed: " + error);
  result.selectors = *selectors;
  result.layout = geist::detail::extract_layout_ir(result.sources);
  result.ownership =
      geist::detail::build_ownership_ir(result.sources, result.layout);
  return result;
}

void verify_synthetic_contract() {
  const auto value = pipeline({make_source(
      10, {words("chdlevel :FIGLIST"), words("ST Figures"),
           words("cselect 3 4 FIGONE"), words("?"), words("   "),
           words("ABCD")})});
  std::string error;
  const auto list = geist::detail::extract_generated_list_topic_ir(
      value.sources, value.selectors, value.layout, value.ownership, &error);
  require(list && list->entries.size() == 1 &&
              list->kind == geist::detail::GeneratedListTopicKindIR::figures &&
              geist::detail::verify_generated_list_topic_ir(
                  value.sources, value.selectors, value.layout,
                  value.ownership, *list, &error),
          "complete FIGLIST topic was rejected: " + error);
  if (list) {
    auto mutated = *list;
    ++mutated.heading_source.byte_end;
    require(!geist::detail::verify_generated_list_topic_ir(
                 value.sources, value.selectors, value.layout,
                 value.ownership, mutated, &error),
            "generated-list verifier admitted mutated heading provenance");
    mutated = *list;
    require(mutated.entries.front().cells.front().source.has_value(),
            "synthetic generated row has no source provenance");
    ++mutated.entries.front().cells.front().source->token_bytes.end;
    require(!geist::detail::verify_generated_list_topic_ir(
                 value.sources, value.selectors, value.layout,
                 value.ownership, mutated, &error),
            "generated-list verifier admitted mutated cell provenance");
    mutated = *list;
    mutated.entries.front().spans.front().target.raw_target = "OTHER";
    require(!geist::detail::verify_generated_list_topic_ir(
                 value.sources, value.selectors, value.layout,
                 value.ownership, mutated, &error),
            "generated-list verifier admitted mutated raw target identity");
  }
  geist::detail::TopicIdentityIR identity;
  identity.id = "FIGURES";
  identity.title = "Figures";
  identity.start_logical_record = 10;
  identity.end_logical_record = 10;
  const auto document = list
                            ? geist::detail::lower_generated_list_topic_to_document_ir(
                                  identity, *list, &error)
                            : std::nullopt;
  const auto* paragraph =
      document && document->blocks.size() == 2
          ? std::get_if<geist::detail::ParagraphBlockIR>(
                &document->blocks[1].node)
          : nullptr;
  const geist::detail::CrossReferenceInlineIR* link = nullptr;
  if (paragraph)
    for (const auto& inline_node : paragraph->content)
      if (const auto* candidate =
              std::get_if<geist::detail::CrossReferenceInlineIR>(
                  &inline_node.node))
        link = candidate;
  require(document && link && link->label == "ABCD" &&
              link->target.kind ==
                  geist::detail::CrossReferenceTargetKindIR::anchor &&
              link->target.value == "FIGONE" &&
              geist::detail::verify_generated_list_topic_document_ir(
                  *list, *document, &error),
          "generated list did not lower to a typed raw anchor: " + error +
              (document ? "\n" + geist::detail::format_document_ir(*document)
                        : std::string{}));

  const auto extra = pipeline({make_source(
      11, {words("chdlevel :FIGLIST"), words("ST Figures"),
           words("cselect 3 4 FIGONE"), words("?"), words("   "),
           words("ABCD"), words("CMENU")})});
  require(!geist::detail::extract_generated_list_topic_ir(
               extra.sources, extra.selectors, extra.layout, extra.ownership,
               &error),
          "generated-list envelope admitted a trailing typed object");

  const auto mismatch = pipeline({make_source(
      12, {words("chdlevel :TLIST"), words("ST Figures"),
           words("cselect 3 4 TBLONE"), words("?"), words("   "),
           words("ABCD")})});
  require(!geist::detail::extract_generated_list_topic_ir(
               mismatch.sources, mismatch.selectors, mismatch.layout,
               mismatch.ownership, &error),
          "generated-list envelope admitted a mismatched ST title");
}

#ifdef GEIST_REPO_ROOT
void load_context(const std::filesystem::path& path,
                  geist::detail::LogicalDecodeContext* context_ptr) {
  auto& context = *context_ptr;
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

void verify_corpus_inventory() {
  const auto directory = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  std::vector<std::string> admitted;
  std::vector<std::string> rejected_candidates;
  auto entries = std::size_t{0};
  for (const auto& file : std::filesystem::directory_iterator(directory)) {
    if (!file.is_regular_file()) continue;
    auto extension = file.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char ch) { return std::tolower(ch); });
    if (extension != ".boo") continue;
    const auto book = geist::BooDocument::open(file.path());
    geist::detail::LogicalDecodeContext context;
    load_context(file.path(), &context);
    for (const auto& topic : book.topics()) {
      const auto sources = geist::detail::decode_logical_record_sources(
          context, topic.start_logical_record, topic.end_logical_record);
      const auto selectors = geist::detail::extract_selector_catalog_ir(sources);
      if (!selectors) continue;
      const auto layout = geist::detail::extract_layout_ir(sources);
      const auto ownership = geist::detail::build_ownership_ir(sources, layout);
      std::string error;
      const auto list = geist::detail::extract_generated_list_topic_ir(
          sources, *selectors, layout, ownership, &error);
      if (!list) {
        if (file.path().filename() == "SC26-457.boo" && topic.id == "FIGURES")
          std::cerr << "SC26 " << error << '\n';
        if (topic.id == "FIGURES" || topic.id == "TABLES")
          rejected_candidates.push_back(file.path().filename().string() + ':' +
                                        topic.id);
        if (file.path().filename() == "XWEBDEMO.boo" &&
            topic.id == "FIGURES")
          require(error.find("opcode=c.sp") != std::string::npos,
                  "XWEB generated-list blocker changed: " + error);
        continue;
      }
      require(geist::detail::verify_generated_list_topic_ir(
                  sources, *selectors, layout, ownership, *list, &error),
              "admitted generated-list topic did not verify: " +
                  file.path().filename().string() + ':' + topic.id + ' ' + error);
      geist::detail::TopicIdentityIR identity;
      identity.id = topic.id;
      identity.title = topic.title;
      identity.topic_number = topic.topic_number;
      identity.start_logical_record = topic.start_logical_record;
      identity.end_logical_record = topic.end_logical_record;
      const auto document =
          geist::detail::lower_generated_list_topic_to_document_ir(
              identity, *list, &error);
      require(document &&
                  geist::detail::verify_generated_list_topic_document_ir(
                      *list, *document, &error),
              "generated list did not lower canonically: " + error);
      admitted.push_back(file.path().filename().string() + ':' + topic.id + ':' +
                         std::to_string(list->entries.size()));
      entries += list->entries.size();
    }
  }
  std::sort(admitted.begin(), admitted.end());
  auto expected = std::vector<std::string>{
      "DREICMST.boo:FIGURES:129",    "FA1PLMM0.boo:FIGURES:113",
      "GC23-046.boo:FIGURES:32",     "GC23-046.boo:TABLES:32",
      "GC28-183.boo:FIGURES:55",     "GG24-395.boo:FIGURES:81",
      "GG24-395.boo:TABLES:16",      "GG24-4302-00.boo:FIGURES:51",
      "GG24-4302-00.boo:TABLES:15",  "IEAC6MST.BOO:FIGURES:100",
      "ITPPIBOK.BOO:FIGURES:20",     "ITPPIBOK.BOO:TABLES:1",
      "SC09-138.boo:FIGURES:162",    "SC09-138.boo:TABLES:44",
      "SC24-546.boo:FIGURES:9",      "SC24-546.boo:TABLES:4",
      "SC24-5527-02.boo:FIGURES:11", "SC24-5527-02.boo:TABLES:71",
      "SC26-457.boo:FIGURES:53",     "SC28-1881-05.boo:FIGURES:18",
      "SC33-033.boo:FIGURES:7",      "SC33-033.boo:TABLES:4",
      "SG24-204.boo:FIGURES:128",    "SH20-918.boo:FIGURES:12",
      "SH20-918.boo:TABLES:9"};
  std::sort(expected.begin(), expected.end());
  std::sort(rejected_candidates.begin(), rejected_candidates.end());
  const auto expected_rejections = std::vector<std::string>{
      "XWEBDEMO.boo:FIGURES", "packet.boo:FIGURES", "packet.boo:TABLES"};
  if (admitted != expected || entries != 1177) {
    std::cerr << "entries=" << entries << '\n';
    for (const auto& item : rejected_candidates) std::cerr << "reject " << item << '\n';
  }
  require(admitted == expected && entries == 1177,
          "whole-topic generated-list inventory changed");
  require(rejected_candidates == expected_rejections,
          "generated-list fail-closed candidate inventory changed");
}
#endif

} // namespace

int main() {
  verify_synthetic_contract();
#ifdef GEIST_REPO_ROOT
  verify_corpus_inventory();
#endif
}
