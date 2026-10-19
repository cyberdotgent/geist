// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/detail/core/internal.hpp"
#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/layout/display_lines.hpp"
#include "geist/detail/lowering/topic_document_lowering.hpp"
#include "geist/detail/render/document_html_renderer.hpp"
#include "geist/detail/render/document_markdown_renderer.hpp"
#include "test_failures.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace geist::detail;
TokenWords words(const std::string &value) { return {value.begin(), value.end()}; }
void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "prose_header_evidence_synthetic: " << message << '\n';
    geist_test::record_failure();
  }
}

DecodedLogicalRecordSource source(const std::string &title,
                                  const std::string &id = "1.1") {
  DecodedLogicalRecordSource result;
  std::uint16_t key = 0x100;
  const auto line = [&](std::vector<TokenWords> tokens) {
    result.encoded_tokens.push_back(
        {static_cast<std::uint16_t>(tokens.size() * 2), 1});
    result.tokens.push_back({0x25ba});
    for (auto &token : tokens) {
      result.encoded_tokens.push_back({key++, 2});
      result.tokens.push_back(std::move(token));
    }
  };
  line({words("sh" + id)});
  line({words("ctopicn"), words("10")});
  line({words("cparent"), words("1.0")});
  line({words("cforwardlevel")});
  line({words("cbacklevel")});
  line({words("csummary"), words("10"), words("0"), words("10")});
  line({words("chdlevel"), words(":H2")});
  line({words("csourcefn"), words("SAMPLE")});
  line({words("ST"), words(title)});
  line({words("cz"), words("FLOW"), words("P"), words("3"), words("3")});
  line({words("   "), words("Body"), words("text.")});
  result.logical_record = 20;
  result.assembled = assemble_logical_record_with_sources(result.tokens);
  result.ir.logical_record = 20;
  std::uint32_t byte = 1;
  for (std::size_t token = 0; token < result.tokens.size(); ++token) {
    const auto encoded = result.encoded_tokens[token];
    result.ir.tokens.push_back(
        {token, encoded, result.tokens[token],
         {byte, static_cast<std::uint32_t>(byte + encoded.width)}, false, 3});
    byte += encoded.width;
  }
  result.ir.payload_range = {1, byte};
  assign_display_line_framing(result.ir);
  result.control_segments = decode_control_segments(
      20, result.assembled, result.encoded_tokens, result.ir.display_lines);
  demote_display_line_owned_controls(result);
  return result;
}

BookTopicCatalogIR catalog(const std::string &header,
                          const std::string &label) {
  geist::TopicInfo topic{"1.1", header, ":H2", 10, 20, 21};
  geist::TocEntry entry;
  entry.id = "1.1";
  entry.title = label;
  entry.heading_level = ":H2";
  entry.topic_number = 10;
  entry.start_logical_record = 20;
  entry.end_logical_record = 21;
  return *build_book_topic_catalog_ir({topic}, {entry});
}

void distinct_header_and_contents_label(const std::string &label) {
  const std::string heading = "Language; Release Two";
  const auto book = catalog(heading, label);
  const TopicIdentityIR identity{"1.1", label, ":H2", 10, 20, 21};
  const std::vector<DecodedLogicalRecordSource> records{source(heading)};
  std::string error;
  const auto result = try_lower_topic_to_document_ir(identity, records, &book,
                                                     &error);
  check(result.has_value(), "separate source heading rejected: " + error);
  if (!result) return;
  check(result->topic.title == label, "contents label was overwritten");
  const auto html = render_document_html_fragment(*result);
  const auto markdown = render_document_markdown(*result);
  check(html.find("1.1 Language; Release Two</h2>") != std::string::npos,
        "HTML heading does not preserve ST source: " + html);
  check(markdown.find("Language; Release Two") != std::string::npos,
        "Markdown heading does not preserve ST source: " + markdown);
  check(html.find("Body text.") != std::string::npos,
        "body was lost");

  auto reject = [&](TopicIdentityIR changed_identity,
                    BookTopicCatalogIR changed_book,
                    std::vector<DecodedLogicalRecordSource> changed_records,
                    const std::string &description) {
    check(!try_lower_topic_to_document_ir(changed_identity, changed_records,
                                          &changed_book, &error), description);
  };
  auto changed = book;
  changed.topics.front().topic_header->start_logical_record = 19;
  reject(identity, changed, records, "stale header bounds were accepted");
  changed = book;
  changed.topics.front().topic_header->end_logical_record = 22;
  reject(identity, changed, records, "overlong header bounds were accepted");
  changed = book;
  changed.topics.front().topic_header->topic_number = 11;
  reject(identity, changed, records, "different header number was accepted");
  changed = book;
  changed.topics.front().topic_header->heading_level = ":H3";
  reject(identity, changed, records, "different heading level was accepted");
  changed = book;
  changed.topics.front().topic_header.reset();
  reject(identity, changed, records, "missing header evidence was accepted");
  changed = book;
  changed.topics.front().topic_header->title = "Different Heading";
  reject(identity, changed, records, "mismatched native title was accepted");
  changed = book;
  changed.topics.front().raw_topic_id = "9.9";
  reject(identity, changed, records, "another topic's evidence was accepted");
  auto wrong_identity = identity;
  wrong_identity.title = "Uncatalogued Label";
  reject(wrong_identity, book, records, "uncatalogued label was accepted");
  reject(identity, book, {source("Altered Source")},
         "mutated ST heading was accepted");
  reject(identity, book, {source(heading, "1.2")},
         "different source SH identity was accepted");
  auto shifted_records = records;
  shifted_records.front().logical_record = 19;
  reject(identity, book, shifted_records,
         "different source logical range was accepted");
  check(!try_lower_topic_to_document_ir(identity, records, nullptr, &error),
        "different title without header evidence was accepted");
}
} // namespace

int main() {
  distinct_header_and_contents_label("Language? Release Two");
  distinct_header_and_contents_label("Abbreviated Language");
  return 0;
}
